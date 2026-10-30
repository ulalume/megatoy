#include "ui_file_requests.hpp"

#include "app_services.hpp"
#include "core/status.hpp"
#include "gui/components/common.hpp"
#include "gui/patch_save_dialog.hpp"
#include "gui/save_as_dialog.hpp"
#include "gui/save_export_actions.hpp"
#include "patch_actions.hpp"
#include "patches/filename_utils.hpp"
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
#include <filesystem>
#include <functional>
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

void request_patch_deletion(AppContext &ctx, const patches::PatchEntry &entry) {
  if (!ctx.services.patch_session.repository().can_delete_patch(entry)) {
    return;
  }

  const std::string location =
      megatoy::platform::is_web() ? "from browser storage" : "from disk";
  ctx.ui_state().danger_confirmation_state.request(
      "Delete Patch?",
      "Delete \"" + entry.name + "\"?\n\nThis permanently deletes it " +
          location + ". This cannot be undone.",
      "Delete", [&ctx, entry]() {
        if (!ctx.services.patch_session.delete_patch(entry)) {
          megatoy::status::error("Could not delete \"" + entry.name + "\".");
          return;
        }
        megatoy::status::success("Deleted \"" + entry.name + "\".");
      });
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

/// After a folder changed path, report once browser storage has it: IndexedDB
/// keys files by path, so every patch under the folder is rewritten. Elsewhere
/// the change is already on disk.
static void
report_folder_change(AppContext &ctx, const patches::PatchEntry &entry,
                     const std::filesystem::path &new_path,
                     const std::string &progress, const std::string &success,
                     const std::string &failure, const std::string &undone) {
#if defined(MEGATOY_PLATFORM_WEB)
  const auto old_path = entry.full_path;
  const bool was_workspace_folder =
      ctx.services.preference_manager.workspace().find(new_path) != nullptr;
  const bool started = platform::web::begin_awaited_flush(
      progress, "Saving the change to browser storage.",
      [&ctx, new_path, old_path, was_workspace_folder, success, failure,
       undone](bool ok, std::string error) {
        if (ok) {
          megatoy::status::success(success);
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
  megatoy::status::success(success);
#endif
}

static void perform_patch_rename(AppContext &ctx,
                                 const patches::PatchEntry &entry,
                                 const std::string &new_stem) {
  if (!ctx.services.patch_session.rename_patch(entry, new_stem)) {
    megatoy::status::error("Could not rename \"" + entry.name + "\".");
    return;
  }
  const std::string success = "Renamed to \"" + new_stem + "\".";
  if (!entry.is_directory) {
    megatoy::status::success(success);
    return;
  }
  report_folder_change(ctx, entry, entry.full_path.parent_path() / new_stem,
                       "Renaming \"" + entry.name + "\"...", success,
                       "Could not save the rename",
                       "the old name comes back after a reload.");
}

static void perform_patch_move(AppContext &ctx,
                               const patches::PatchEntry &entry,
                               const patches::PatchEntry &destination) {
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
  const std::string success =
      "Moved \"" + entry.name + "\" to \"" + destination.name + "\".";
  if (!entry.is_directory) {
    megatoy::status::success(success);
    return;
  }
  report_folder_change(ctx, entry, target, "Moving \"" + entry.name + "\"...",
                       success, "Could not save the move",
                       "it moves back after a reload.");
}

void request_patch_move(AppContext &ctx, const patches::PatchEntry &entry,
                        const patches::PatchEntry &destination) {
  confirm_large_folder_change(ctx, entry, "Move Folder?",
                              "Moving may take a while.", "Move",
                              [&ctx, entry, destination]() {
                                perform_patch_move(ctx, entry, destination);
                              });
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
              perform_patch_rename(ctx, entry, new_stem);
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

/// The name a copy or a download starts from: the file's own stem, or the
/// instrument's name inside a bank, which has no file of its own.
static std::string entry_stem(const patches::PatchEntry &entry) {
  const bool bank_instrument =
      !entry.source_relative_path.empty() && entry.container_item_id.empty();
  if (bank_instrument) {
    const auto sanitized = patches::sanitize_filename(entry.name);
    if (!sanitized.empty()) {
      return sanitized;
    }
  }
  return entry.full_path.stem().string();
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
  if (platform::web::download_patch(patch, extension, entry_stem(entry))) {
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
  const std::string stem = duplicate_stem(entry_stem(entry));
  const std::string extension = default_save_as_extension(
      entry.full_path.extension().string(), session.save_formats());
  const auto source_folder = writable_entry_folder(ctx, entry);

#if defined(MEGATOY_PLATFORM_WEB)
  // No file dialog to name the copy, so the dialog at the top level does.
  auto &duplicate = ctx.ui_state().patch_duplicate_state;
  duplicate.patch = std::move(patch);
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
  const auto format = session.find_save_format(extension);
  const std::vector<platform::file_dialog::FileFilter> filters{
      {format ? format->label : "megatoy", {extension.substr(1)}},
      {"All Files", {"*"}}};

  std::filesystem::path selected;
  const auto dialog = platform::file_dialog::save_file(
      start_directory, stem + extension, filters, selected);
  if (dialog == platform::file_dialog::DialogResult::Cancelled) {
    return;
  }
  if (dialog != platform::file_dialog::DialogResult::Ok) {
    megatoy::status::error("Failed to save patch");
    return;
  }

  selected =
      patches::append_extension_if_missing(std::move(selected), extension);
  patch.name = selected.stem().string();
  if (!patches::write_patch(patch, selected)) {
    megatoy::status::error("Failed to write " + selected.string());
    return;
  }
  ctx.services.preference_manager.set_last_save_directory(
      selected.parent_path());
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
