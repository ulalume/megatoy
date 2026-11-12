#include "ui_file_requests.hpp"

#include "app_services.hpp"
#include "core/status.hpp"
#include "core/utf8_utils.hpp"
#include "gui/components/common.hpp"
#include "gui/components/file_manager.hpp"
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
#include "platform/trash.hpp"
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
 * modal; elsewhere the change is on disk already. When storage could not
 * keep it, `failed` runs and the user is told `failure`, with `undone` on
 * what that leaves.
 */
static void after_storage_has(const std::string &progress,
                              std::function<void()> done,
                              const std::string &failure,
                              const std::string &undone = {},
                              std::function<void()> failed = {}) {
#if defined(MEGATOY_PLATFORM_WEB)
  const bool started = platform::web::begin_awaited_flush(
      progress, "Saving the change to browser storage.",
      [done, failure, undone, failed](bool ok, std::string error) {
        if (ok) {
          done();
          return;
        }
        if (failed) {
          failed();
        }
        megatoy::status::error(failure + ": " + error +
                               (undone.empty() ? "" : " -- " + undone));
      });
  if (!started) {
    megatoy::status::warning("Another storage operation is in progress.");
  }
#else
  (void)progress;
  (void)failure;
  (void)undone;
  (void)failed;
  done();
#endif
}

/// Tells the user a change in the Library is done, offering to undo it when
/// `undo` is given. `touched` is what the change made.
static void report_done(const std::string &message,
                        std::vector<std::filesystem::path> touched,
                        std::function<void()> undo) {
  if (undo) {
    library_undo::offer(message, std::move(touched), std::move(undo));
  } else {
    megatoy::status::success(message);
  }
}

/// A folder moved or renamed to `new_path` that storage could not keep:
/// IndexedDB still has it at `old_path`, so a workspace folder registered
/// there has to point back or a reload finds it missing.
static std::function<void()>
workspace_rollback(AppContext &ctx, const std::filesystem::path &old_path,
                   const std::filesystem::path &new_path) {
  if (ctx.services.preference_manager.workspace().find(new_path) == nullptr) {
    return {};
  }
  return [&ctx, old_path, new_path]() {
    ctx.services.preference_manager.rename_workspace_folder(new_path, old_path);
  };
}

/// Puts `kept` back and gives the documents `detached` their file again.
static std::function<void()>
restoring(AppContext &ctx, patches::DeletedEntry kept,
          std::vector<patches::PatchSession::DetachedDocument> detached,
          std::string name) {
  // Shared, not copied, by the callbacks it passes through.
  return [&ctx,
          kept = std::make_shared<const patches::DeletedEntry>(std::move(kept)),
          detached = std::move(detached), name = std::move(name)]() {
    auto &session = ctx.services.patch_session;
    if (!session.repository().restore(*kept)) {
      megatoy::status::error("Could not restore \"" + name + "\".");
      return;
    }
    session.reattach(detached);
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
}

static void perform_patch_deletion(AppContext &ctx,
                                   const patches::PatchEntry &entry) {
  auto &session = ctx.services.patch_session;
  std::optional<patches::DeletedEntry> kept;
  std::vector<patches::PatchSession::DetachedDocument> detached;
  std::string message;
  if (platform::trash::available()) {
    kept = session.trash_patch(entry, &detached);
    if (!kept) {
      megatoy::status::post(
          megatoy::status::Severity::Error,
          "Could not move \"" + entry.name + "\" to the Trash.",
          {ui::reveal_in_file_manager_label(), [path = entry.full_path]() {
             ui::reveal_in_file_manager(megatoy::utf8::from_path(path));
           }});
      return;
    }
    message = "Moved \"" + entry.name + "\" to the Trash.";
  } else {
    kept = session.repository().keep_for_restore(entry);
    if (!session.delete_patch(entry, &detached)) {
      megatoy::status::error("Could not delete \"" + entry.name + "\".");
      return;
    }
    message = "Deleted \"" + entry.name + "\".";
  }
  auto undo =
      kept ? restoring(ctx, std::move(*kept), std::move(detached), entry.name)
           : std::function<void()>();
  const bool folder = entry.is_directory && entry.format.empty();
  if (!folder) {
    report_done(message, {entry.full_path}, std::move(undo));
    return;
  }
  after_storage_has(
      "Deleting \"" + entry.name + "\"...",
      [message, path = entry.full_path, undo]() {
        report_done(message, {path}, undo);
      },
      "Could not save the deletion");
}

void request_patch_deletion(AppContext &ctx, const patches::PatchEntry &entry) {
  const auto &repository = ctx.services.patch_session.repository();
  if (!repository.can_delete_patch(entry)) {
    return;
  }

  const bool folder = entry.is_directory && entry.format.empty();
  const std::size_t patches = folder ? count_patches(entry) : 0;
  const std::string what =
      !folder        ? "it"
      : patches == 0 ? "the folder"
                     : "the folder and the " + std::to_string(patches) +
                           (patches == 1 ? " patch" : " patches") + " in it";
  const auto unlisted =
      folder ? repository.unlisted_files(entry) : std::optional<std::size_t>(0);
  const std::string others =
      !unlisted ? "\n\nIt may also hold files the Library does not show, "
                  "which go with it."
      : *unlisted == 0 ? ""
                       : "\n\nIt also holds " + std::to_string(*unlisted) +
                             (*unlisted == 1 ? " file" : " files") +
                             " the Library does not show, which go with it.";
  const auto perform = [&ctx, entry]() { perform_patch_deletion(ctx, entry); };
  if (platform::trash::available()) {
    ctx.ui_state().danger_confirmation_state.request(
        "Move to Trash?",
        "Move \"" + entry.name + "\" to the Trash?\n\nThis moves " + what +
            " to the Trash." + others,
        "Move to Trash", perform);
    return;
  }
  const std::string location =
      megatoy::platform::is_web() ? "from browser storage" : "from disk";
  ctx.ui_state().danger_confirmation_state.request(
      folder ? "Delete Folder?" : "Delete Patch?",
      "Delete \"" + entry.name + "\"?\n\nThis deletes " + what + " " +
          location + "." + others,
      "Delete", perform);
}

/// The entry listed at `path`, as the tree has it now, copied: the change it
/// is passed to rebuilds the tree.
static std::optional<patches::PatchEntry>
entry_at(AppContext &ctx, const std::filesystem::path &path) {
  const auto *entry =
      ctx.services.patch_session.repository().find_by_full_path(path);
  return entry != nullptr ? std::optional(*entry) : std::nullopt;
}

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
      if (const auto now = entry_at(ctx, renamed)) {
        perform_patch_rename(ctx, *now, old_stem, false);
      } else {
        megatoy::status::error("Could not undo the rename.");
      }
    };
  }
  const std::string message = "Renamed to \"" + new_stem + "\".";
  if (!entry.is_directory) {
    report_done(message, {renamed}, std::move(undo));
    return;
  }
  after_storage_has(
      "Renaming \"" + entry.name + "\"...",
      [message, renamed, undo]() { report_done(message, {renamed}, undo); },
      "Could not save the rename", "the old name comes back after a reload.",
      workspace_rollback(ctx, entry.full_path, renamed));
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
      const auto moved = entry_at(ctx, target);
      const auto back = entry_at(ctx, from);
      if (moved && back) {
        perform_patch_move(ctx, *moved, *back, false);
      } else {
        megatoy::status::error("Could not undo the move.");
      }
    };
  }
  const std::string message =
      "Moved \"" + entry.name + "\" to \"" + destination.name + "\".";
  if (!entry.is_directory) {
    report_done(message, {target}, std::move(undo));
    return;
  }
  after_storage_has(
      "Moving \"" + entry.name + "\"...",
      [message, target, undo]() { report_done(message, {target}, undo); },
      "Could not save the move", "it moves back after a reload.",
      workspace_rollback(ctx, entry.full_path, target));
}

void request_patch_move(AppContext &ctx, const patches::PatchEntry &entry,
                        const patches::PatchEntry &destination) {
  confirm_large_folder_change(
      ctx, entry, "Move Folder?", "Moving may take a while.", "Move",
      [&ctx, entry, destination]() {
        perform_patch_move(ctx, entry, destination, true);
      });
}

static bool any_folder(const std::vector<std::filesystem::path> &paths) {
  return std::any_of(paths.begin(), paths.end(), [](const auto &path) {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
  });
}

/// Undo for copies just made: they are taken away again, and on the web a
/// folder among them waits for browser storage.
static std::function<void()>
undo_copies(AppContext &ctx, std::vector<std::filesystem::path> copies) {
  // Shared, not copied, by the toast that holds it.
  return [&ctx,
          copies = std::make_shared<const std::vector<std::filesystem::path>>(
              std::move(copies))]() {
    const bool folders = any_folder(*copies);
    if (!ctx.services.patch_session.remove_copies(*copies)) {
      megatoy::status::error("Could not remove every copy.");
      return;
    }
    const std::string removed =
        copies->size() == 1 ? "Removed the copy." : "Removed the copies.";
    if (!folders) {
      megatoy::status::success(removed);
      return;
    }
    after_storage_has(
        "Removing the copy...",
        [removed]() { megatoy::status::success(removed); },
        "Could not save the removal", "the copy comes back after a reload.");
  };
}

/// Tells the user about copies just made, with Undo, once browser storage
/// has a folder among them.
static void report_copies(AppContext &ctx, const std::string &message,
                          const std::vector<std::filesystem::path> &copies,
                          const std::string &destination) {
  auto undo = undo_copies(ctx, copies);
  if (!any_folder(copies)) {
    report_done(message, copies, std::move(undo));
    return;
  }
  after_storage_has(
      "Copying to \"" + destination + "\"...",
      [message, copies, undo]() { report_done(message, copies, undo); },
      "Could not save the copy", "it is gone after a reload.");
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
  report_copies(
      ctx, "Copied \"" + entry.name + "\" to \"" + destination.name + "\".",
      {*copy}, destination.name);
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
  const auto copies = ctx.services.patch_session.repository().copy_files_in(
      files, destination, skipped);
  if (copies.empty()) {
    const std::string what =
        files.size() == 1 ? "\"" + files.front().filename().string() + "\""
                          : "what was dropped";
    megatoy::status::warning("Nothing in " + what +
                             " is a patch megatoy reads, so nothing was "
                             "copied.");
    return;
  }
  const std::string unread =
      skipped == 0 ? ""
                   : " " + std::to_string(skipped) +
                         (skipped == 1 ? " item holds" : " items hold") +
                         " no patch megatoy reads.";
  report_copies(ctx,
                (copies.size() == 1
                     ? "Copied \"" + copies.front().filename().string() + "\""
                     : "Copied " + std::to_string(copies.size()) + " items") +
                    " to \"" + destination.name + "\"." + unread,
                copies, destination.name);
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
      start_directory, stem, patches::save_dialog_filters(formats, extension),
      selected);
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
