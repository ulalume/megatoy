#include "ui_renderer.hpp"
#include "core/status.hpp"
#include "drop_actions.hpp"
#include "gui/components/common.hpp"
#include "gui/components/confirmation_dialog.hpp"
#include "gui/components/file_manager.hpp"
#include "gui/components/folder_scan_dialog.hpp"
#include "gui/components/main_menu.hpp"
#include "gui/components/midi_keyboard.hpp"
#include "gui/components/mml_console.hpp"
#include "gui/components/patch_drop.hpp"
#include "gui/components/patch_editor.hpp"
#include "gui/components/patch_lab_window.hpp"
#include "gui/components/patch_selector.hpp"
#include "gui/components/preferences.hpp"
#include "gui/components/status_toasts.hpp"
#include "gui/components/waveform.hpp"
#include "gui/patch_save_dialog.hpp"
#include "gui/save_as_dialog.hpp"
#include "gui/save_export_actions.hpp"
#include "gui/window_title.hpp"
#include "history/snapshot_entry.hpp"
#include "midi/midi_input_manager.hpp"
#include "patch_actions.hpp"
#include "patches/filename_utils.hpp"
#include "patches/patch_write.hpp"
#include "platform/file_dialog.hpp"
#include "platform/platform_config.hpp"
#include "platform/web/web_folder_delete.hpp"
#include "platform/web/web_folder_import.hpp"
#include "platform/web/web_storage_flush.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include "platform/web/web_storage_bootstrap.hpp"
#include "platform/web/web_workspace_download.hpp"
#include "system/path_service.hpp"
#include "workspace/path_policy.hpp"
#endif
#include <cassert>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ui {
namespace {

struct HistoryActions {
  AppContext &ctx;

  void undo() const { ctx.services.history.undo(ctx); }
  void redo() const { ctx.services.history.redo(ctx); }
};

struct PatchHistoryActions {
  AppContext &ctx;

  void begin_snapshot(const std::string &label,
                      const std::string &merge_key) const {
    auto before_snapshot = ctx.services.patch_session.capture_snapshot();
    auto label_copy = label;
    auto key_copy = merge_key;
    ctx.services.history.begin_transaction(
        label_copy, key_copy,
        [label_copy = std::move(label_copy), key_copy = std::move(key_copy),
         before_snapshot](AppContext &context) mutable {
          auto &patch_session = context.services.patch_session;
          auto after_snapshot = patch_session.capture_snapshot();
          return history::make_snapshot_entry<
              patches::PatchSession::PatchSnapshot>(
              label_copy, key_copy, before_snapshot, after_snapshot,
              [](AppContext &context,
                 const patches::PatchSession::PatchSnapshot &snapshot) {
                context.services.patch_session.restore_snapshot(snapshot);
              });
        });
  }

  void commit() const { ctx.services.history.commit_transaction(ctx); }
};

struct PatchActions {
  AppContext &ctx;

  bool load(const patches::PatchEntry &entry) const {
    return patch_actions::load(ctx, entry);
  }

  void safe_load(const patches::PatchEntry &entry) const {
    patch_actions::safe_load(ctx, entry);
  }

  void load_dropped(const ym2612::Patch &patch,
                    const std::filesystem::path &path) const {
    patch_actions::load_dropped_patch(ctx, patch, path);
  }
};

struct MidiActions {
  AppContext &ctx;

  bool note_on(ym2612::Note note, uint8_t velocity) const {
    return ctx.services.patch_session.note_on(note, velocity,
                                              ctx.ui_state().prefs);
  }

  bool note_off(ym2612::Note note) const {
    return ctx.services.patch_session.note_off(note);
  }

  bool note_is_active(const ym2612::Note &note) const {
    return ctx.services.patch_session.note_is_active(note);
  }

  std::vector<ym2612::Note> active_notes() const {
    return ctx.services.patch_session.active_notes();
  }
};

drop_actions::Environment make_drop_environment(AppContext &ctx) {
  return {
      ctx.services, ctx.ui_state(),
      [&ctx](const ym2612::Patch &patch, const std::filesystem::path &path) {
        patch_actions::load_dropped_patch(ctx, patch, path);
      }};
}

PatchLabState &patch_lab_state() {
  static PatchLabState state;
  return state;
}

MidiKeyboardState &midi_keyboard_state() {
  static MidiKeyboardState state;
  return state;
}

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

bool selection_belongs_to_entry(const std::string &selection,
                                const patches::PatchEntry &entry) {
  if (selection == entry.relative_path) {
    return true;
  }
  const std::string prefix = entry.relative_path + "/";
  return selection.rfind(prefix, 0) == 0;
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
        auto &session = ctx.services.patch_session;
        const bool clear_selection = selection_belongs_to_entry(
            session.current_patch_selection_path(), entry);
        if (!session.repository().delete_patch(entry)) {
          megatoy::status::error("Could not delete \"" + entry.name + "\".");
          return;
        }
        if (clear_selection) {
          session.set_current_patch_path({});
        }
        megatoy::status::success("Deleted \"" + entry.name + "\".");
      });
}

/// Patches under an entry, for telling the user what a rename will cost.
std::size_t count_patches(const patches::PatchEntry &entry) {
  if (!entry.is_directory) {
    return 1;
  }
  std::size_t total = 0;
  for (const auto &child : entry.children) {
    total += count_patches(child);
  }
  return total;
}

/// Rename, and on the web wait for browser storage: IndexedDB keys files by
/// path, so renaming a folder rewrites every patch under it.
void perform_patch_rename(AppContext &ctx, const patches::PatchEntry &entry,
                          const std::string &new_stem) {
  auto &session = ctx.services.patch_session;
#if defined(MEGATOY_PLATFORM_WEB)
  auto &preferences = ctx.services.preference_manager;
  const auto old_path = entry.full_path;
  const bool was_workspace_folder =
      entry.is_directory && preferences.workspace().find(old_path) != nullptr;
#endif

  if (!session.rename_patch(entry, new_stem)) {
    megatoy::status::error("Could not rename \"" + entry.name + "\".");
    return;
  }

#if defined(MEGATOY_PLATFORM_WEB)
  if (entry.is_directory) {
    const auto new_path = old_path.parent_path() / new_stem;
    const bool started = platform::web::begin_awaited_flush(
        "Renaming \"" + entry.name + "\"...",
        "Saving the change to browser storage.",
        [&ctx, new_stem, new_path, old_path,
         was_workspace_folder](bool ok, std::string error) {
          if (ok) {
            megatoy::status::success("Renamed to \"" + new_stem + "\".");
            return;
          }
          // Storage kept the old name, so the workspace has to point there
          // again or a reload finds the folder missing.
          if (was_workspace_folder) {
            ctx.services.preference_manager.rename_workspace_folder(new_path,
                                                                    old_path);
          }
          megatoy::status::error("Could not save the rename: " + error +
                                 " -- the old name comes back after a reload.");
        });
    if (!started) {
      megatoy::status::warning("Another storage operation is in progress.");
    }
    return;
  }
#endif

  megatoy::status::success("Renamed to \"" + new_stem + "\".");
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
#if defined(MEGATOY_PLATFORM_WEB)
        // Browser storage rewrites every patch under the folder, so a big one
        // is worth warning about first.
        constexpr std::size_t kWarnThreshold = 200;
        const std::size_t patches = count_patches(entry);
        if (entry.is_directory && patches >= kWarnThreshold) {
          ctx.ui_state().danger_confirmation_state.request(
              "Rename Folder?",
              "\"" + entry.name + "\" holds " + std::to_string(patches) +
                  " patches.\n\nRenaming may take a while.",
              "Rename", [&ctx, entry, new_stem]() {
                perform_patch_rename(ctx, entry, new_stem);
              });
          return;
        }
#endif
        perform_patch_rename(ctx, entry, new_stem);
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
std::string entry_stem(const patches::PatchEntry &entry) {
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
std::optional<std::filesystem::path>
writable_entry_folder(AppContext &ctx, const patches::PatchEntry &entry) {
  const auto folder = entry.full_path.parent_path();
  const auto *owner =
      ctx.services.preference_manager.workspace().owner_of(folder);
  if (owner == nullptr || !owner->writable) {
    return std::nullopt;
  }
  return folder;
}

void announce_saved_patch(AppContext &ctx, const std::filesystem::path &path) {
  const auto relative =
      ctx.services.patch_session.repository().to_relative_path(path);
  megatoy::status::success("Saved " +
                           display_preset_path(relative.generic_string()));
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

/// Copy the entry's patch to a new file. The editor is left as it is.
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
      save_as_folder_choices(session.repository().workspace()),
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

MainMenuContext make_main_menu_context(AppContext &ctx) {
  auto &state = ctx.app_state();
  auto &ui_state = state.ui_state();
  auto history_actions = HistoryActions{ctx};
  auto patch_history = PatchHistoryActions{ctx};
  return {ctx.services.history,
          ctx.services.gui_manager,
          ctx.services.preference_manager,
          ui_state.prefs,
          ui_state.open_add_folder_dialog,
          [&ctx]() { ctx.services.patch_session.sync_workspace(); },
          [&ctx](const std::filesystem::path &path) {
            request_workspace_folder_removal(ctx, path);
          },
          ctx.services.patch_session,
          ui_state.save_export_state,
          [history_actions]() { history_actions.undo(); },
          [history_actions]() { history_actions.redo(); },
          ui_state.operator_edit,
          [patch_history](const std::string &label) {
            // Empty merge key: two operator commands in a row stay two
            // separate undo steps.
            patch_history.begin_snapshot(label, {});
          },
          [patch_history]() { patch_history.commit(); },
          ctx.services.audio_manager.load_meter(),
          ui_state.open_sound_preferences,
          ctx.services.audio_manager.default_buffer_frames()};
}

void render_save_export_popup_host(AppContext &ctx) {
  ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar;
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->Pos);
  ImGui::SetNextWindowSize(ImVec2(0, 0));
  if (ImGui::Begin("##save_export_popup_host", nullptr, flags)) {
    render_save_export_popups(ctx.services.patch_session,
                              ctx.app_state().ui_state().save_export_state);
  }
  ImGui::End();
}

PatchDropContext make_patch_drop_context(AppContext &ctx) {
  auto &state = ctx.app_state();
  auto &ui_state = state.ui_state();
  return {
      ui_state.drop_state,
      [&ctx]() { drop_actions::cancel_selection(ctx.ui_state().drop_state); },
      [&ctx](size_t index) {
        auto env = make_drop_environment(ctx);
        drop_actions::apply_selection(env, index);
      }};
}

ConfirmationDialogContext make_confirmation_context(AppContext &ctx) {
  auto &state = ctx.app_state();
  auto &ui_state = state.ui_state();
  auto patch_actions_facade = PatchActions{ctx};
  return {ui_state.confirmation_state,
          ui_state.danger_confirmation_state,
          ui_state.text_prompt_state,
          ui_state.new_patch_prompt_state,
          ui_state.drop_state,
          [patch_actions_facade](const patches::PatchEntry &entry) {
            patch_actions_facade.load(entry);
          },
          [patch_actions_facade](const ym2612::Patch &patch,
                                 const std::filesystem::path &path) {
            patch_actions_facade.load_dropped(patch, path);
          },
          [&ctx]() {
            ctx.services.gui_manager.set_should_close(true);
            ctx.services.patch_session.mark_as_clean();
          }};
}

PatchEditorContext make_patch_editor_context(AppContext &ctx) {
  auto &state = ctx.app_state();
  auto &ui_state = state.ui_state();
  auto patch_history = PatchHistoryActions{ctx};
  return {ctx.services.patch_session,
          ui_state.prefs,
          ui_state.envelope_states,
          ui_state.operator_edit,
          [patch_history](const std::string &label,
                          const std::string &merge_key, const ym2612::Patch &) {
            patch_history.begin_snapshot(label, merge_key);
          },
          [patch_history]() { patch_history.commit(); }};
}

PatchLabContext make_patch_lab_context(AppContext &ctx) {
  auto &state = ctx.app_state();
  auto &ui_state = state.ui_state();
  auto patch_history = PatchHistoryActions{ctx};
  return {ctx.services.patch_session, ui_state.prefs,
          [patch_history](const std::string &label,
                          const std::string &merge_key, const ym2612::Patch &) {
            patch_history.begin_snapshot(label, merge_key);
          },
          [patch_history]() { patch_history.commit(); }};
}

PatchSelectorContext make_patch_selector_context(AppContext &ctx) {
  auto &state = ctx.app_state();
  auto &ui_state = state.ui_state();
  auto patch_actions_facade = PatchActions{ctx};
  return {
      ctx.services.patch_session.repository(), ctx.services.patch_session,
      ui_state.prefs,
      [patch_actions_facade](const patches::PatchEntry &entry) {
        patch_actions_facade.safe_load(entry);
      },
      // No file manager exists in a browser; the context menu keys off
      // the callback's absence.
      megatoy::platform::is_desktop()
          ? [](const std::filesystem::path
                   &path) { reveal_in_file_manager(path.string()); }
          : std::function<void(const std::filesystem::path &)>{},
#if defined(MEGATOY_PLATFORM_WEB)
      [&ctx](const patches::PatchEntry &entry) {
        if (platform::web::download_workspace_path(
                ctx.services.path_service.file_system(), entry.full_path)) {
          megatoy::status::success("Download started.");
        } else {
          megatoy::status::error("Failed to prepare download.");
        }
      },
      [&ctx](const patches::PatchEntry &entry, const std::string &extension) {
        download_patch_entry(ctx, entry, extension);
      },
      [&ctx](const std::string &extension) {
        download_current_patch(ctx.services.patch_session, extension);
      },
#else
      {},
      {},
      {},
#endif
      [&ctx]() {
        trigger_save(ctx.services.patch_session,
                     ctx.app_state().ui_state().save_export_state);
      },
      [&ctx]() {
        request_save_as(ctx.app_state().ui_state().save_export_state);
      },
#if defined(MEGATOY_PLATFORM_WEB)
      [&ctx]() {
        request_save_to_storage(ctx.app_state().ui_state().save_export_state);
      },
#else
      {},
#endif
      ctx.services.preference_manager.workspace().empty(),
      [&ctx]() { ctx.app_state().ui_state().open_add_folder_dialog = true; },
      [&ctx](const std::filesystem::path &path) {
        return ctx.services.preference_manager.workspace_folder_is_protected(
            path);
      },
      [&ctx](const std::filesystem::path &path) {
        request_workspace_folder_removal(ctx, path);
      },
      [&ctx](const patches::PatchEntry &entry) {
        request_patch_rename(ctx, entry);
      },
      [&ctx](const patches::PatchEntry &entry) {
        request_patch_deletion(ctx, entry);
      },
      [&ctx](const patches::PatchEntry &entry) {
        request_patch_duplicate(ctx, entry);
      },
      [&ctx](const std::filesystem::path &folder) {
        request_new_patch(ctx, folder);
      },
      [&ctx](const std::filesystem::path &folder) {
        request_new_folder(ctx, folder);
      }};
}

MidiKeyboardContext make_midi_keyboard_context(AppContext &ctx) {
  auto &state = ctx.app_state();
  auto &ui_state = state.ui_state();
  auto midi_actions = MidiActions{ctx};
  return {
      ui_state.prefs,
      state.input_state(),
      midi_keyboard_state(),
      [midi_actions](ym2612::Note note, uint8_t velocity) {
        return midi_actions.note_on(note, velocity);
      },
      [midi_actions](ym2612::Note note) { return midi_actions.note_off(note); },
      [midi_actions](const ym2612::Note &note) {
        return midi_actions.note_is_active(note);
      },
      [midi_actions]() { return midi_actions.active_notes(); },
  };
}

PreferencesContext make_preferences_context(AppContext &ctx) {
  auto &state = ctx.app_state();
  auto &ui_state = state.ui_state();
  MidiInputManager::StatusInfo midi_status =
      ctx.midi ? ctx.midi->status()
               : MidiInputManager::StatusInfo{.message =
                                                  "MIDI backend unavailable."};
  return {
      ctx.services.preference_manager,
      ui_state.prefs,
      ui_state.open_add_folder_dialog,
      ui_state.open_sound_preferences,
      ctx.services.path_service.paths(),
      state.connected_midi_inputs(),
      midi_status.message,
      midi_status.show_enable_button,
      midi_status.enable_button_disabled,
      [&ctx]() {
        if (ctx.midi) {
          ctx.midi->request_web_midi_access();
        }
      },
      [&ctx]() { ctx.services.patch_session.sync_workspace(); },
      [&ctx](const std::filesystem::path &path) {
        request_workspace_folder_removal(ctx, path);
      },
      [&ctx](ui::styles::ThemeId theme_id) {
        ctx.services.gui_manager.set_theme(theme_id);
      },
      [&ctx](float preference) {
        ctx.services.gui_manager.apply_ui_scale(preference);
      },
      true,
      ctx.services.audio_manager.default_buffer_frames(),
      ctx.services.gui_manager.display_scale(),
  };
}

MmlConsoleContext make_mml_console_context(AppContext &ctx) {
  auto &ui_state = ctx.app_state().ui_state();
  return {
      ui_state.prefs,
      [&ctx]() -> const ym2612::Patch & {
        return ctx.services.patch_session.current_patch();
      },
  };
}

WaveformContext make_waveform_context(AppContext &ctx) {
  return {
      ctx.app_state().ui_state().prefs,
      ctx.services.audio_manager.scope_buffer(),
      ctx.services.spectrum_analyzer,
      ctx.services.audio_manager.sample_rate(),
  };
}

/**
 * Every window's context, built once.
 *
 * A context is references and callbacks into objects that live for the whole
 * run -- AppContext sits on main()'s stack, UIState and the services inside
 * it never move. Rebuilding all eleven every frame allocated a few dozen
 * std::functions per frame for no change in behavior. The handful of fields
 * that are values rather than references are refreshed in render_all.
 */
struct FrameContexts {
  explicit FrameContexts(AppContext &ctx)
      : owner(&ctx), main_menu(make_main_menu_context(ctx)),
        patch_drop(make_patch_drop_context(ctx)),
        confirmation(make_confirmation_context(ctx)),
        patch_editor(make_patch_editor_context(ctx)),
        patch_selector(make_patch_selector_context(ctx)),
        preferences(make_preferences_context(ctx)),
        midi_keyboard(make_midi_keyboard_context(ctx)),
        mml_console(make_mml_console_context(ctx)),
        patch_lab(make_patch_lab_context(ctx)),
        waveform(make_waveform_context(ctx)) {}

  AppContext *owner;
  MainMenuContext main_menu;
  PatchDropContext patch_drop;
  ConfirmationDialogContext confirmation;
  PatchEditorContext patch_editor;
  PatchSelectorContext patch_selector;
  PreferencesContext preferences;
  MidiKeyboardContext midi_keyboard;
  MmlConsoleContext mml_console;
  PatchLabContext patch_lab;
  WaveformContext waveform;
};

} // namespace

void render_all(AppContext &ctx) {
  static FrameContexts contexts(ctx);
  // The cache is only valid for the AppContext it captured.
  assert(contexts.owner == &ctx);

  static auto next_workspace_refresh = std::chrono::steady_clock::time_point{};
  const auto now = std::chrono::steady_clock::now();
  if (now >= next_workspace_refresh) {
    next_workspace_refresh = now + std::chrono::seconds(2);
    if (ctx.services.preference_manager.refresh_workspace_availability()) {
      ctx.services.patch_session.sync_workspace();
    }
  }

  // Per-frame values; everything else in the contexts is stable references.
  contexts.patch_selector.workspace_is_empty =
      ctx.services.preference_manager.workspace().empty();
  {
    MidiInputManager::StatusInfo status =
        ctx.midi ? ctx.midi->status()
                 : MidiInputManager::StatusInfo{
                       .message = "MIDI backend unavailable."};
    contexts.preferences.midi_status_message = std::move(status.message);
    contexts.preferences.show_web_midi_button = status.show_enable_button;
    contexts.preferences.web_midi_button_disabled =
        status.enable_button_disabled;
  }

  render_main_menu(contexts.main_menu);
  render_patch_drop_feedback(contexts.patch_drop);
  render_confirmation_dialog(contexts.confirmation);
  render_patch_duplicate_dialog(ctx);
#if defined(MEGATOY_PLATFORM_WEB)
  platform::web::render_folder_import_ui();
  platform::web::render_folder_delete_ui();
  platform::web::render_awaited_flush_ui();
#else
  render_folder_scan_dialog();
#endif
  render_patch_editor(PATCH_EDITOR_TITLE, contexts.patch_editor,
                      ctx.app_state().ui_state().save_export_state);
  render_patch_selector(PATCH_BROWSER_TITLE, contexts.patch_selector);
  render_preferences_window(PREFERENCES_TITLE, contexts.preferences);
  render_midi_keyboard(SOFT_KEYBOARD_TITLE, contexts.midi_keyboard);
  render_mml_console(MML_CONSOLE_TITLE, contexts.mml_console);
  render_patch_lab(PATCH_LAB_TITLE, contexts.patch_lab, patch_lab_state());
  render_waveform(WAVEFORM_TITLE, contexts.waveform);

  render_save_export_popup_host(ctx);
  render_status_toasts();
}

} // namespace ui
