#include "ui_frame_contexts.hpp"

#include "app_context.hpp"
#include "app_services.hpp"
#include "core/status.hpp"
#include "drop_actions.hpp"
#include "gui/components/file_manager.hpp"
#include "gui/save_export_actions.hpp"
#include "midi/midi_input_manager.hpp"
#include "patch_actions.hpp"
#include "platform/platform_config.hpp"
#include "ui_document_flows.hpp"
#include "ui_file_requests.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include "platform/web/web_workspace_download.hpp"
#endif
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ui::renderer_detail {
namespace {

drop_actions::Environment make_drop_environment(AppContext &ctx) {
  return {
      ctx.services, ctx.ui_state(),
      [&ctx](const ym2612::Patch &patch, const std::filesystem::path &path) {
        patch_actions::open_dropped_patch(ctx, patch, path);
      }};
}

UIState::DocumentUi &active_document_ui(AppContext &ctx) {
  return ctx.ui_state().document_ui(
      ctx.services.patch_session.active_document_id());
}

MidiKeyboardState &midi_keyboard_state() {
  static MidiKeyboardState state;
  return state;
}

MainMenuContext make_main_menu_context(AppContext &ctx) {
  auto &state = ctx.app_state();
  auto &ui_state = state.ui_state();
  return {
      [&ctx]() -> history::HistoryManager & { return ctx.services.history(); },
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
      [&ctx]() { ctx.services.history().undo(ctx); },
      [&ctx]() { ctx.services.history().redo(ctx); },
      [&ctx]() -> OperatorEditState & {
        return active_document_ui(ctx).operator_edit;
      },
      ui_state.operator_clipboard,
      [&ctx](const std::string &label) {
        // Empty merge key: two operator commands in a row stay two
        // separate undo steps.
        patch_actions::begin_change(ctx, label, {});
      },
      [&ctx]() { patch_actions::commit_change(ctx); },
      ctx.services.audio_manager.load_meter(),
      ui_state.open_sound_preferences,
      ctx.services.audio_manager.default_buffer_frames(),
      [&ctx]() { patch_actions::new_patch(ctx); },
      [&ctx]() {
        request_close_document(ctx,
                               ctx.services.patch_session.active_document_id());
      },
      [&ctx](const std::string &message, std::function<void()> proceed) {
        ctx.ui_state().danger_confirmation_state.request_merge_folders(
            message, std::move(proceed));
      }};
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
  return {ui_state.confirmation_state,
          ui_state.danger_confirmation_state,
          ui_state.text_prompt_state,
          ui_state.new_patch_prompt_state,
          [&ctx](patches::DocumentId id) { close_document_now(ctx, id); },
          [&ctx](patches::DocumentId id) { save_and_close_document(ctx, id); },
          [&ctx](patches::DocumentId id) {
            patch_actions::reveal_document(ctx, id);
          },
          [&ctx]() {
            ctx.services.gui_manager.set_should_close(true);
            ctx.services.patch_session.mark_all_clean();
          },
          [&ctx](patches::DocumentId id) { save_and_exit(ctx, id); }};
}

/// The undo step opens on the document whose window is being drawn, which
/// the context names only once it exists; FrameContexts sets begin_history.
PatchEditorContext make_patch_editor_context(AppContext &ctx) {
  auto &state = ctx.app_state();
  auto &ui_state = state.ui_state();
  return {ctx.services.patch_session,
          ctx.services.audio_manager,
          ui_state.prefs,
          [&ctx](patches::DocumentId id) -> UIState::DocumentUi & {
            return ctx.ui_state().document_ui(id);
          },
          ui_state.operator_clipboard,
          {},
          [&ctx]() { patch_actions::commit_change(ctx); },
          [&ctx](patches::DocumentId id) {
            patch_actions::activate_document(ctx, id);
          },
          [&ctx](patches::DocumentId id) { request_close_document(ctx, id); },
          ui_state.reveal_document,
          ui_state.editor_dock_id};
}

PatchLabContext make_patch_lab_context(AppContext &ctx) {
  auto &state = ctx.app_state();
  auto &ui_state = state.ui_state();
  return {ctx.services.patch_session, ui_state.prefs,
          [&ctx](const std::string &label, const std::string &merge_key) {
            patch_actions::begin_change(ctx, label, merge_key);
          },
          [&ctx]() { patch_actions::commit_change(ctx); }};
}

PatchSelectorContext make_patch_selector_context(AppContext &ctx) {
  auto &state = ctx.app_state();
  auto &ui_state = state.ui_state();
  return {
      ctx.services.patch_session.repository(), ctx.services.patch_session,
      ui_state.prefs,
      [&ctx](const patches::PatchEntry &entry, bool pinned) {
        patch_actions::open_entry(ctx, entry, pinned);
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
      [&ctx](const patches::PatchEntry &entry,
             const patches::PatchEntry &destination) {
        request_patch_move(ctx, entry, destination);
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
  return {
      ui_state.prefs,
      state.input_state(),
      midi_keyboard_state(),
      [&ctx](ym2612::Note note, uint8_t velocity) {
        return ctx.services.patch_session.note_on(note, velocity,
                                                  ctx.ui_state().prefs);
      },
      [&ctx](ym2612::Note note) {
        return ctx.services.audio_manager.submit(
            audio::AudioCommand::note_off(note));
      },
      [&ctx](const ym2612::Note &note) {
        return ctx.services.audio_manager.notes().published_contains(note);
      },
      [&ctx]() { return ctx.services.audio_manager.notes().published_notes(); },
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

} // namespace

FrameContexts::FrameContexts(AppContext &ctx)
    : owner(&ctx), main_menu(make_main_menu_context(ctx)),
      patch_drop(make_patch_drop_context(ctx)),
      confirmation(make_confirmation_context(ctx)),
      patch_editor(make_patch_editor_context(ctx)),
      patch_selector(make_patch_selector_context(ctx)),
      preferences(make_preferences_context(ctx)),
      midi_keyboard(make_midi_keyboard_context(ctx)),
      mml_console(make_mml_console_context(ctx)),
      patch_lab(make_patch_lab_context(ctx)),
      waveform(make_waveform_context(ctx)) {
  patch_editor.begin_history = [&ctx, this](const std::string &label,
                                            const std::string &merge_key) {
    patch_actions::activate_document(ctx, patch_editor.document);
    patch_actions::begin_change(ctx, label, merge_key);
  };
  patch_editor.reload_from_disk = [&ctx](patches::DocumentId document,
                                         const ym2612::Patch &patch) {
    patch_actions::reload_from_disk(ctx, document, patch);
  };
}

} // namespace ui::renderer_detail
