#include "ui_renderer.hpp"
#include "drop_actions.hpp"
#include "gui/components/confirmation_dialog.hpp"
#include "gui/components/file_manager.hpp"
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
#include "gui/save_export_actions.hpp"
#include "gui/window_title.hpp"
#include "history/snapshot_entry.hpp"
#include "midi/midi_input_manager.hpp"
#include "patch_actions.hpp"
#include "platform/platform_config.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include "core/status.hpp"
#include "platform/web/web_workspace_download.hpp"
#endif
#include <cassert>
#include <filesystem>
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
                context.services.patch_session.apply_patch_to_audio();
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

MainMenuContext make_main_menu_context(AppContext &ctx) {
  auto &state = ctx.app_state();
  auto &ui_state = state.ui_state();
  auto history_actions = HistoryActions{ctx};
  return {ctx.services.history,
          ctx.services.gui_manager,
          ctx.services.preference_manager,
          ui_state.prefs,
          ui_state.open_add_folder_dialog,
          [&ctx]() { ctx.services.patch_session.sync_workspace(); },
          ctx.services.patch_session,
          ui_state.save_export_state,
          [history_actions]() { history_actions.undo(); },
          [history_actions]() { history_actions.redo(); }};
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
  return {ui_state.confirmation_state, ui_state.drop_state,
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
  return {ctx.services.patch_session, ui_state.prefs, ui_state.envelope_states,
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
#else
      {},
#endif
      [&ctx]() {
        trigger_save(ctx.services.patch_session,
                     ctx.app_state().ui_state().save_export_state);
      },
      [&ctx]() {
        request_save_as(ctx.app_state().ui_state().save_export_state);
      },
      ctx.services.preference_manager.workspace().empty(),
      [&ctx]() { ctx.app_state().ui_state().open_add_folder_dialog = true; },
      [&ctx](const std::filesystem::path &path) {
        if (ctx.services.preference_manager.remove_workspace_folder(path)) {
          ctx.services.patch_session.sync_workspace();
        }
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
      [&ctx](ui::styles::ThemeId theme_id) {
        ctx.services.gui_manager.set_theme(theme_id);
      },
      // Adding folders needs a native picker, so the list is desktop-only.
      megatoy::platform::is_desktop(),
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
