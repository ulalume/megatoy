#include "ui_file_requests.hpp"

#include "app_services.hpp"
#include "core/status.hpp"
#include "gui/components/common.hpp"
#include "gui/library_undo.hpp"
#include "gui/patch_save_dialog.hpp"
#include "gui/save_as_dialog.hpp"
#include "gui/save_export_actions.hpp"
#include "patch_actions.hpp"
#include "patches/filename_utils.hpp"
#include "patches/patch_entry_actions.hpp"
#include "patches/patch_write.hpp"
#include "platform/file_dialog.hpp"
#include "platform/platform_config.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include "platform/web/web_folder_delete.hpp"
#include "platform/web/web_storage_bootstrap.hpp"
#include "platform/web/web_storage_flush.hpp"
#include "platform/web/web_workspace_download.hpp"
#include "system/path_service.hpp"
#include "workspace/path_policy.hpp"
#endif
#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ui::renderer_detail {

void request_workspace_folder_removal(AppContext &ctx,
                                      const std::filesystem::path &path) {
  auto &preferences = ctx.services.preference_manager;
  if (preferences.workspace_folder_is_protected(path)) {
    megatoy::status::error("\"My Patches\" cannot be removed.");
    return;
  }

#if defined(MEGATOY_PLATFORM_WEB)
  std::string name = path.filename().string();
  if (name.empty()) {
    name = path.string();
  }
  ctx.ui_state().danger_confirmation_state.request(
      "Delete Folder?",
      "Delete \"" + name +
          "\"?\n\nThis permanently deletes the folder and all of its patches "
          "from browser storage. This cannot be undone.",
      "Delete", [&ctx, path, name]() {
        // The deletion is only safe on directories this app manages: a folder
        // still registered in the workspace and sitting directly inside the
        // browser storage root. Anything else means stale UI state.
        if (!ctx.services.preference_manager.workspace().contains(path) ||
            !megatoy::workspace::paths_equal(
                path.parent_path(),
                megatoy::system::PathService::web_storage_root())) {
          megatoy::status::error("Could not delete \"" + name +
                                 "\": not a workspace folder.");
          return;
        }
        // Removing the files is instant; getting IndexedDB to agree is not,
        // and until it does a reload brings the whole folder back. So the
        // workspace removal and the tree refresh wait for the flush -- the
        // completion callback runs on the main thread, on a later frame.
        const bool started = platform::web::begin_folder_delete(
            path, [&ctx, path, name](bool ok, std::string error) {
              if (!ok) {
                megatoy::status::error(
                    "Could not persist the deletion: " + error +
                    " -- the folder may reappear after a reload.");
                return;
              }
              if (!ctx.services.preference_manager.remove_workspace_folder(
                      path)) {
                megatoy::status::error("Could not remove \"" + name +
                                       "\" from the workspace.");
                return;
              }
              ctx.services.patch_session.sync_workspace();
              megatoy::status::success("Deleted \"" + name + "\".");
            });
        if (!started) {
          megatoy::status::warning("Another folder deletion is in progress.");
        }
      });
#else
  if (preferences.remove_workspace_folder(path)) {
    ctx.services.patch_session.sync_workspace();
  }
#endif
}

/// Patches under an entry, for telling the user what a rename will cost.
static std::size_t count_patches(const patches::PatchEntry &entry) {
  if (!entry.is_directory) {
    return 1;
  }
  std::size_t total = 0;
  for (const auto &child : entry.children) {
    total += count_patches(child);
  }
  return total;
}

/// Run `action`, on the web only once a folder big enough to keep browser
/// storage busy for a while has been confirmed.
static void confirm_large_folder_change(AppContext &ctx,
                                        const patches::PatchEntry &entry,
                                        const std::string &title,
                                        const std::string &consequence,
                                        const std::string &button,
                                        std::function<void()> action) {
#if defined(MEGATOY_PLATFORM_WEB)
  constexpr std::size_t kWarnThreshold = 200;
  const std::size_t patches = count_patches(entry);
  if (entry.is_directory && patches >= kWarnThreshold) {
    ctx.ui_state().danger_confirmation_state.request(
        title,
        "\"" + entry.name + "\" holds " + std::to_string(patches) +
            " patches.\n\n" + consequence,
        button, std::move(action));
    return;
  }
#else
  (void)ctx;
  (void)entry;
  (void)title;
  (void)consequence;
  (void)button;
#endif
  action();
}

/**
 * Runs `done` once browser storage has a change to many files, behind a
 * modal; elsewhere the change is already on disk.
 */
static void after_storage_has(const std::string &progress,
                              std::function<void()> done,
                              const std::string &failure) {
#if defined(MEGATOY_PLATFORM_WEB)
  const bool started = platform::web::begin_awaited_flush(
      progress, "Saving the change to browser storage.",
      [done, failure](bool ok, std::string error) {
        if (ok) {
          done();
        } else {
          megatoy::status::error(failure + ": " + error);
        }
      });
  if (!started) {
    megatoy::status::warning("Another storage operation is in progress.");
  }
#else
  (void)progress;
  (void)failure;
  done();
#endif
}

static void perform_patch_deletion(AppContext &ctx,
                                   const patches::PatchEntry &entry) {
  auto &session = ctx.services.patch_session;
  auto kept = session.repository().keep_for_restore(entry);
  if (!session.delete_patch(entry)) {
    megatoy::status::error("Could not delete \"" + entry.name + "\".");
    return;
  }
  const std::string message = "Deleted \"" + entry.name + "\".";
  std::function<void()> report = [message]() {
    megatoy::status::success(message);
  };
  if (kept) {
    const auto path = kept->path;
    // Shared, not copied, by the callbacks it passes through.
    auto restore =
        [&ctx,
         kept = std::make_shared<const patches::DeletedEntry>(std::move(*kept)),
         name = entry.name]() {
          if (!ctx.services.patch_session.repository().restore(*kept)) {
            megatoy::status::error("Could not restore \"" + name + "\".");
            return;
          }
          const std::string restored = "Restored \"" + name + "\".";
          if (!kept->is_directory) {
            megatoy::status::success(restored);
            return;
          }
          after_storage_has(
              "Restoring \"" + name + "\"...",
              [restored]() { megatoy::status::success(restored); },
              "Could not save the restore");
        };
    report = [message, path, restore]() {
      library_undo::offer(message, {path}, restore);
    };
  }
  if (!entry.is_directory || !entry.format.empty()) {
    report();
    return;
  }
  after_storage_has("Deleting \"" + entry.name + "\"...", report,
                    "Could not save the deletion");
}

void request_patch_deletion(AppContext &ctx, const patches::PatchEntry &entry) {
  if (!ctx.services.patch_session.repository().can_delete_patch(entry)) {
    return;
  }

  const std::string location =
      megatoy::platform::is_web() ? "from browser storage" : "from disk";
  const bool folder = entry.is_directory && entry.format.empty();
  const std::size_t patches = folder ? count_patches(entry) : 0;
  const std::string what =
      !folder        ? "it"
      : patches == 0 ? "the folder"
                     : "the folder and the " + std::to_string(patches) +
                           (patches == 1 ? " patch" : " patches") + " in it";
  ctx.ui_state().danger_confirmation_state.request(
      folder ? "Delete Folder?" : "Delete Patch?",
      "Delete \"" + entry.name + "\"?\n\nThis deletes " + what + " " +
          location + ".",
      "Delete", [&ctx, entry]() { perform_patch_deletion(ctx, entry); });
}

/// After a folder changed path, report once browser storage has it: IndexedDB
/// keys files by path, so every patch under the folder is rewritten. Elsewhere
/// the change is already on disk.
static void
report_folder_change(AppContext &ctx, const patches::PatchEntry &entry,
                     const std::filesystem::path &new_path,
                     const std::string &progress, std::function<void()> done,
                     const std::string &failure, const std::string &undone) {
#if defined(MEGATOY_PLATFORM_WEB)
  const auto old_path = entry.full_path;
  const bool was_workspace_folder =
      ctx.services.preference_manager.workspace().find(new_path) != nullptr;
  const bool started = platform::web::begin_awaited_flush(
      progress, "Saving the change to browser storage.",
      [&ctx, new_path, old_path, was_workspace_folder, done, failure,
       undone](bool ok, std::string error) {
        if (ok) {
          done();
          return;
        }
        // Storage kept the old path, so the workspace has to point there
        // again or a reload finds the folder missing.
        if (was_workspace_folder) {
          ctx.services.preference_manager.rename_workspace_folder(new_path,
                                                                  old_path);
        }
        megatoy::status::error(failure + ": " + error + " -- " + undone);
      });
  if (!started) {
    megatoy::status::warning("Another storage operation is in progress.");
  }
#else
  (void)ctx;
  (void)entry;
  (void)new_path;
  (void)progress;
  (void)failure;
  (void)undone;
  done();
#endif
}

/// The entry listed at `path` in `entries` or below, a workspace folder
/// itself among them.
static const patches::PatchEntry *
find_listed(const std::vector<patches::PatchEntry> &entries,
            const std::filesystem::path &path) {
  for (const auto &entry : entries) {
    if (entry.full_path == path && entry.source_relative_path.empty() &&
        entry.container_item_id.empty()) {
      return &entry;
    }
    if (entry.is_directory) {
      if (const auto *found = find_listed(entry.children, path)) {
        return found;
      }
    }
  }
  return nullptr;
}

/// The entry listed at `path`, as the tree has it now.
static const patches::PatchEntry *entry_at(AppContext &ctx,
                                           const std::filesystem::path &path) {
  return find_listed(ctx.services.patch_session.repository().tree(),
                     path.lexically_normal());
}

/// Tells the user how a change in the Library went, offering to undo it when
/// `undo` is given.
static void report_done(const std::string &message,
                        const std::filesystem::path &touched,
                        std::function<void()> undo) {
  if (undo) {
    library_undo::offer(message, {touched}, std::move(undo));
  } else {
    megatoy::status::success(message);
  }
}

static void perform_patch_rename(AppContext &ctx,
                                 const patches::PatchEntry &entry,
                                 const std::string &new_stem, bool undoable);
static void perform_patch_move(AppContext &ctx,
                               const patches::PatchEntry &entry,
                               const patches::PatchEntry &destination,
                               bool undoable);

static void perform_patch_rename(AppContext &ctx,
                                 const patches::PatchEntry &entry,
                                 const std::string &new_stem, bool undoable) {
  if (!ctx.services.patch_session.rename_patch(entry, new_stem)) {
    megatoy::status::error("Could not rename \"" + entry.name + "\".");
    return;
  }
  const std::string old_stem = entry.is_directory
                                   ? entry.full_path.filename().string()
                                   : entry.full_path.stem().string();
  const auto renamed =
      entry.full_path.parent_path() /
      (entry.is_directory ? new_stem
                          : new_stem + entry.full_path.extension().string());
  std::function<void()> undo;
  if (undoable) {
    undo = [&ctx, renamed, old_stem]() {
      const auto *now = entry_at(ctx, renamed);
      if (now == nullptr) {
        megatoy::status::error("Could not undo the rename.");
        return;
      }
      // Copied: renaming rebuilds the tree the entry is in.
      const patches::PatchEntry entry = *now;
      perform_patch_rename(ctx, entry, old_stem, false);
    };
  }
  const std::string message = "Renamed to \"" + new_stem + "\".";
  if (!entry.is_directory) {
    report_done(message, renamed, std::move(undo));
    return;
  }
  report_folder_change(
      ctx, entry, renamed, "Renaming \"" + entry.name + "\"...",
      [message, renamed, undo]() { report_done(message, renamed, undo); },
      "Could not save the rename", "the old name comes back after a reload.");
}

static void perform_patch_move(AppContext &ctx,
                               const patches::PatchEntry &entry,
                               const patches::PatchEntry &destination,
                               bool undoable) {
  const auto target = destination.full_path / entry.full_path.filename();
  std::error_code error;
  if (std::filesystem::exists(target, error)) {
    megatoy::status::error("\"" + destination.name + "\" already has \"" +
                           entry.name + "\".");
    return;
  }
  if (error || !ctx.services.patch_session.move_patch(entry, destination)) {
    megatoy::status::error("Could not move \"" + entry.name + "\".");
    return;
  }
  std::function<void()> undo;
  if (undoable) {
    undo = [&ctx, target, from = entry.full_path.parent_path()]() {
      const auto *moved = entry_at(ctx, target);
      const auto *back = entry_at(ctx, from);
      if (moved == nullptr || back == nullptr) {
        megatoy::status::error("Could not undo the move.");
        return;
      }
      // Copied: moving rebuilds the tree the entries are in.
      const patches::PatchEntry entry = *moved;
      const patches::PatchEntry destination = *back;
      perform_patch_move(ctx, entry, destination, false);
    };
  }
  const std::string message =
      "Moved \"" + entry.name + "\" to \"" + destination.name + "\".";
  if (!entry.is_directory) {
    report_done(message, target, std::move(undo));
    return;
  }
  report_folder_change(
      ctx, entry, target, "Moving \"" + entry.name + "\"...",
      [message, target, undo]() { report_done(message, target, undo); },
      "Could not save the move", "it moves back after a reload.");
}

void request_patch_move(AppContext &ctx, const patches::PatchEntry &entry,
                        const patches::PatchEntry &destination) {
  confirm_large_folder_change(
      ctx, entry, "Move Folder?", "Moving may take a while.", "Move",
      [&ctx, entry, destination]() {
        perform_patch_move(ctx, entry, destination, true);
      });
}

static void perform_patch_copy(AppContext &ctx,
                               const patches::PatchEntry &entry,
                               const patches::PatchEntry &destination) {
  // A patch inside a bank or a package becomes a file of its own.
  const auto copy = ctx.services.patch_session.repository().copy_patch(
      entry, destination, kFallbackSaveAsExtension);
  if (!copy) {
    megatoy::status::error("Could not copy \"" + entry.name + "\".");
    return;
  }
  auto undo = [&ctx, copy = *copy, name = entry.name]() {
    if (!ctx.services.patch_session.repository().remove_copy(copy)) {
      megatoy::status::error("Could not remove the copy of \"" + name + "\".");
      return;
    }
    megatoy::status::success("Removed the copy of \"" + name + "\".");
  };
  const std::string message =
      "Copied \"" + entry.name + "\" to \"" + destination.name + "\".";
  if (!entry.is_directory || !std::filesystem::is_directory(*copy)) {
    report_done(message, *copy, std::move(undo));
    return;
  }
  report_folder_change(
      ctx, entry, *copy, "Copying \"" + entry.name + "\"...",
      [message, copy = *copy, undo]() { report_done(message, copy, undo); },
      "Could not save the copy", "it is gone after a reload.");
}

void request_patch_copy(AppContext &ctx, const patches::PatchEntry &entry,
                        const patches::PatchEntry &destination) {
  confirm_large_folder_change(ctx, entry, "Copy Folder?",
                              "Copying may take a while.", "Copy",
                              [&ctx, entry, destination]() {
                                perform_patch_copy(ctx, entry, destination);
                              });
}

void request_files_copy(AppContext &ctx,
                        const std::vector<std::filesystem::path> &files,
                        const patches::PatchEntry &destination) {
  std::size_t skipped = 0;
  auto copies = ctx.services.patch_session.repository().copy_files_in(
      files, destination, skipped);
  const std::string unread =
      skipped == 0 ? ""
                   : " " + std::to_string(skipped) +
                         (skipped == 1 ? " item holds" : " items hold") +
                         " no patch megatoy reads.";
  if (copies.empty()) {
    const std::string what =
        files.size() == 1 ? "\"" + files.front().filename().string() + "\""
                          : "what was dropped";
    megatoy::status::warning("Nothing in " + what +
                             " is a patch megatoy reads, so nothing was "
                             "copied.");
    return;
  }
  const std::string message =
      (copies.size() == 1
           ? "Copied \"" + copies.front().filename().string() + "\""
           : "Copied " + std::to_string(copies.size()) + " items") +
      " to \"" + destination.name + "\"." + unread;
  const bool folders = std::any_of(
      copies.begin(), copies.end(), [](const std::filesystem::path &copy) {
        std::error_code ec;
        return std::filesystem::is_directory(copy, ec);
      });
  auto report = [&ctx, message, copies]() {
    library_undo::offer(message, copies, [&ctx, copies]() {
      auto &repository = ctx.services.patch_session.repository();
      bool removed = true;
      for (const auto &copy : copies) {
        removed = repository.remove_copy(copy) && removed;
      }
      if (removed) {
        megatoy::status::success("Removed the copies.");
      } else {
        megatoy::status::error("Could not remove every copy.");
      }
    });
  };
  if (!folders) {
    report();
    return;
  }
  after_storage_has("Copying to \"" + destination.name + "\"...", report,
                    "Could not save the copy");
}

void request_patch_rename(AppContext &ctx, const patches::PatchEntry &entry) {
  auto &session = ctx.services.patch_session;
  if (!session.repository().can_rename_patch(entry)) {
    return;
  }

  // A folder's name is its whole name; only a file keeps an extension.
  const bool is_directory = entry.is_directory;
  const std::string title = is_directory ? "Rename Folder" : "Rename Patch";
  const std::string label = is_directory ? "Folder name" : "Filename";
  const std::string initial = is_directory ? entry.full_path.filename().string()
                                           : entry.full_path.stem().string();

  ctx.ui_state().text_prompt_state.request(
      title, label, initial, "Rename",
      [&ctx, entry](const std::string &new_stem) {
        confirm_large_folder_change(
            ctx, entry, "Rename Folder?", "Renaming may take a while.",
            "Rename", [&ctx, entry, new_stem]() {
              perform_patch_rename(ctx, entry, new_stem, true);
            });
      },
      [entry, is_directory](const std::string &new_stem) {
        if (new_stem.empty()) {
          return std::string(is_directory ? "Folder name cannot be empty."
                                          : "Filename cannot be empty.");
        }
        if (patches::sanitize_filename(new_stem) != new_stem) {
          return std::string("Name contains invalid characters.");
        }

        const auto target =
            entry.full_path.parent_path() /
            (is_directory ? new_stem
                          : new_stem + entry.full_path.extension().string());
        std::error_code error;
        if (!std::filesystem::exists(target, error)) {
          return error ? std::string("Could not check the target name.")
                       : std::string{};
        }
        if (std::filesystem::equivalent(entry.full_path, target, error) &&
            !error) {
          return std::string{};
        }
        return std::string(is_directory
                               ? "A folder with that name already exists."
                               : "A file with that name already exists.");
      });
}

/// Ask for a name and a format, then write the new patch and open it.
void request_new_patch(AppContext &ctx, const std::filesystem::path &folder) {
  auto &session = ctx.services.patch_session;
  if (!session.can_create_patch_in(folder)) {
    return;
  }
  ctx.ui_state().new_patch_prompt_state.request(
      folder, session.save_formats(),
      [&ctx, folder](const std::string &name, const std::string &extension) {
        auto &patch_session = ctx.services.patch_session;
        const auto result =
            patch_session.create_patch_in(folder, name, extension);
        if (result.is_success()) {
          patch_actions::reveal_document(ctx,
                                         patch_session.active_document_id());
          megatoy::status::success("Created \"" +
                                   result.path.filename().string() + "\".");
        } else {
          megatoy::status::error(result.error_message.empty()
                                     ? "Could not create the patch."
                                     : result.error_message);
        }
      });
}

/// Ask for a name, then create the folder. The editor is left as it is.
void request_new_folder(AppContext &ctx, const std::filesystem::path &parent) {
  if (!ctx.services.patch_session.can_create_patch_in(parent)) {
    return;
  }
  std::vector<std::string> existing;
  for (const auto &item :
       ctx.services.path_service.file_system().read_directory(parent)) {
    existing.push_back(item.path.filename().string());
  }
  ctx.ui_state().text_prompt_state.request(
      "New Folder", "Folder name",
      patches::unused_folder_name(existing, "New folder"), "Create",
      [&ctx, parent](const std::string &name) {
        const auto result =
            ctx.services.patch_session.create_folder_in(parent, name);
        if (result.is_success()) {
          megatoy::status::success("Created \"" + name + "\".");
        } else {
          megatoy::status::error(result.error_message);
        }
      },
      [parent](const std::string &name) {
        return patches::new_folder_name_error(name, parent);
      });
}

/// The entry's own folder, when a copy may be written into it.
static std::optional<std::filesystem::path>
writable_entry_folder(AppContext &ctx, const patches::PatchEntry &entry) {
  const auto folder = entry.full_path.parent_path();
  const auto *owner =
      ctx.services.preference_manager.workspace().owner_of(folder);
  if (owner == nullptr || !owner->writable) {
    return std::nullopt;
  }
  return folder;
}

static void announce_saved_patch(AppContext &ctx,
                                 const std::filesystem::path &path) {
  const auto relative =
      ctx.services.patch_session.repository().to_relative_path(path);
  megatoy::status::success(
      "Saved " +
      display_preset_path(ctx.services.patch_session.repository().display_path(
          relative.generic_string())));
}

/// Open a copy just written, kept open. `patch` is what was written, for a
/// copy outside the workspace that the browser does not list.
static void open_duplicate(AppContext &ctx, const std::filesystem::path &path,
                           const ym2612::Patch &patch) {
  auto &session = ctx.services.patch_session;
  ctx.services.history().cancel_transaction();
  auto document = session.open_path(session.repository().to_relative_path(path),
                                    /*pinned=*/true);
  if (!document) {
    document = session.open_or_activate_patch(patch, path, /*pinned=*/true);
  }
  patch_actions::reveal_document(ctx, *document);
}

#if defined(MEGATOY_PLATFORM_WEB)
/// Hand one tree entry's patch to the browser, in the chosen format.
void download_patch_entry(AppContext &ctx, const patches::PatchEntry &entry,
                          const std::string &extension) {
  ym2612::Patch patch;
  if (!ctx.services.patch_session.repository().load_patch(entry, patch)) {
    megatoy::status::error("Could not read \"" + entry.name + "\".");
    return;
  }
  if (platform::web::download_patch(patch, extension,
                                    patches::entry_stem(entry))) {
    megatoy::status::success("Download started.");
  } else {
    megatoy::status::error("Failed to prepare " + extension + " download.");
  }
}
#endif

/// Copy the entry's patch to a new file and open the copy.
void request_patch_duplicate(AppContext &ctx,
                             const patches::PatchEntry &entry) {
  auto &session = ctx.services.patch_session;
  ym2612::Patch patch;
  if (!session.repository().load_patch(entry, patch)) {
    megatoy::status::error("Could not read \"" + entry.name + "\".");
    return;
  }
  const std::string stem = duplicate_stem(patches::entry_stem(entry));
  const std::string extension = default_save_as_extension(
      entry.full_path.extension().string(), session.save_formats());
  const auto source_folder = writable_entry_folder(ctx, entry);

#if defined(MEGATOY_PLATFORM_WEB)
  // No file dialog to name the copy, so the dialog at the top level does.
  auto &duplicate = ctx.ui_state().patch_duplicate_state;
  duplicate.patch = std::move(patch);
  duplicate.metadata = entry.metadata;
  duplicate.dialog.stem = stem;
  duplicate.dialog.extension = extension;
  duplicate.dialog.folder = save_as_initial_folder(
      save_as_folder_choices(session.repository().workspace().folders()),
      default_save_as_folder(source_folder,
                             platform::web::default_workspace_folder()));
  duplicate.dialog.requested = true;
#else
  const auto start_directory = default_save_as_folder(
      source_folder, ctx.services.preference_manager.last_save_directory());
  const auto formats = session.save_formats();
  std::filesystem::path selected;
  const auto dialog = platform::file_dialog::save_file(
      start_directory, stem + extension,
      patches::save_dialog_filters(formats, extension), selected);
  if (dialog == platform::file_dialog::DialogResult::Cancelled) {
    return;
  }
  if (dialog != platform::file_dialog::DialogResult::Ok) {
    megatoy::status::error("Failed to save patch");
    return;
  }

  selected =
      patches::with_save_extension(std::move(selected), formats, extension);
  patch.name = selected.stem().string();
  if (!patches::write_patch(patch, selected)) {
    megatoy::status::error("Failed to write " + selected.string());
    return;
  }
  ctx.services.preference_manager.set_last_save_directory(
      selected.parent_path());
  if (selected != entry.full_path) {
    session.repository().copy_patch_metadata(entry.metadata, selected);
  }
  session.repository().refresh();
  announce_saved_patch(ctx, selected);
  open_duplicate(ctx, selected, patch);
#endif
}

/// The copy's name, format and folder, asked for outside the browser window
/// the menu item was clicked in.
void render_patch_duplicate_dialog(AppContext &ctx) {
  auto &duplicate = ctx.ui_state().patch_duplicate_state;
  if (!duplicate.dialog.requested && !duplicate.dialog.open) {
    return;
  }
  auto &session = ctx.services.patch_session;
  render_patch_save_dialog(
      "Duplicate", "Duplicate", duplicate.dialog, session.save_formats(),
      session.repository().workspace(),
      [&ctx](const std::filesystem::path &folder, const std::string &stem,
             const std::string &extension, bool overwrite) {
        auto &patch_session = ctx.services.patch_session;
        const auto result = patch_session.repository().save_patch_in(
            folder, ctx.ui_state().patch_duplicate_state.patch, stem, overwrite,
            extension);
        switch (result.status) {
        case patches::SavePatchResult::Status::Success:
          patch_session.repository().copy_patch_metadata(
              ctx.ui_state().patch_duplicate_state.metadata, result.path);
          patch_session.repository().refresh();
          announce_saved_patch(ctx, result.path);
          open_duplicate(ctx, result.path,
                         ctx.ui_state().patch_duplicate_state.patch);
          break;
        case patches::SavePatchResult::Status::Duplicate:
          megatoy::status::error("A patch named \"" + stem +
                                 "\" already exists.");
          break;
        default:
          megatoy::status::error(result.error_message.empty()
                                     ? "Failed to save patch"
                                     : result.error_message);
          break;
        }
      });
}

} // namespace ui::renderer_detail
