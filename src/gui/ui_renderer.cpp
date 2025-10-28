#include "ui_renderer.hpp"

#include "drop_actions.hpp"
#include "gui/components/confirmation_dialog.hpp"
#include "gui/components/file_manager.hpp"
#include "gui/components/main_menu.hpp"
#include "gui/components/midi_keyboard.hpp"
#include "gui/components/mml_console.hpp"
#include "gui/components/patch_drop.hpp"
#include "gui/components/patch_editor.hpp"
#include "gui/components/patch_selector.hpp"
#include "gui/components/preferences.hpp"
#include "gui/components/waveform.hpp"
#include "gui/window_title.hpp"
#include "history/snapshot_entry.hpp"
#include "patch_actions.hpp"
#include <filesystem>
#include <iostream>
#include <utility>

namespace ui {
namespace {

PatchEditorState &patch_editor_state() {
  static PatchEditorState state;
  return state;
}

MidiKeyboardState &midi_keyboard_state() {
  static MidiKeyboardState state;
  return state;
}

MainMenuContext make_main_menu_context(AppContext &ctx) {
  auto &state = ctx.app_state();
  auto &ui_state = state.ui_state();
  return {ctx.services.history,
          ctx.services.gui_manager,
          ctx.services.preference_manager,
          ui_state.prefs,
          ui_state.open_directory_dialog,
          [&ctx]() { ctx.services.history.undo(ctx); },
          [&ctx]() { ctx.services.history.redo(ctx); }};
}

PatchDropContext make_patch_drop_context(AppContext &ctx) {
  auto &state = ctx.app_state();
  auto &ui_state = state.ui_state();
  return {ui_state.drop_state,
          [&ctx]() { drop_actions::cancel_selection(ctx); },
          [&ctx](size_t index) { drop_actions::apply_selection(ctx, index); }};
}

ConfirmationDialogContext make_confirmation_context(AppContext &ctx) {
  auto &state = ctx.app_state();
  auto &ui_state = state.ui_state();
  return {
      ui_state.confirmation_state, ui_state.drop_state,
      [&ctx](const patches::PatchEntry &entry) {
        patch_actions::load(ctx, entry);
      },
      [&ctx](const ym2612::Patch &patch, const std::filesystem::path &path) {
        patch_actions::load_dropped_patch(ctx, patch, path);
      },
      [&ctx]() {
        ctx.services.gui_manager.set_should_close(true);
        ctx.services.patch_session.mark_as_clean();
      }};
}

PatchEditorContext make_patch_editor_context(AppContext &ctx) {
  auto &state = ctx.app_state();
  auto &ui_state = state.ui_state();
  return {
      ctx.services.patch_session, ui_state.prefs, ui_state.envelope_states,
      [&ctx](const std::string &label, const std::string &merge_key,
             const ym2612::Patch &) {
        auto label_copy = label;
        auto key_copy = merge_key;
        auto before_snapshot = ctx.services.patch_session.capture_snapshot();
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
      },
      [&ctx]() { ctx.services.history.commit_transaction(ctx); }};
}

PatchSelectorContext make_patch_selector_context(AppContext &ctx) {
  auto &state = ctx.app_state();
  auto &ui_state = state.ui_state();
  return {ctx.services.patch_session.repository(), ctx.services.patch_session,
          ui_state.prefs,
          [&ctx](const patches::PatchEntry &entry) {
            patch_actions::safe_load(ctx, entry);
          },
          [](const std::filesystem::path &path) {
            reveal_in_file_manager(path.string());
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
        return ctx.services.patch_session.note_off(note);
      },
      [&ctx](const ym2612::Note &note) {
        return ctx.services.patch_session.note_is_active(note);
      },
      [&ctx]() { return ctx.services.patch_session.active_notes(); },
  };
}

PreferencesContext make_preferences_context(AppContext &ctx) {
  auto &state = ctx.app_state();
  auto &ui_state = state.ui_state();
  return {
      ctx.services.preference_manager,
      ui_state.prefs,
      ui_state.open_directory_dialog,
      ctx.services.path_service.paths(),
      state.connected_midi_inputs(),
      [&ctx]() {
        // sync patch directories
        ctx.services.path_service.ensure_directories();
        ctx.services.patch_session.refresh_directories();
      },
      [&ctx](ui::styles::ThemeId theme_id) {
        ctx.services.gui_manager.set_theme(theme_id);
      },
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
  auto &state = ctx.app_state();
  auto &ui_state = ctx.app_state().ui_state();
  return {
      ui_state.prefs,
      ctx.services.audio_manager.wave_sampler(),
      ctx.services.fft_analyzer,
      [&ctx]() -> const ym2612::Patch & {
        return ctx.services.patch_session.current_patch();
      },
  };
}

} // namespace

void render_all(AppContext &ctx) {
  auto menu_context = make_main_menu_context(ctx);
  render_main_menu(menu_context);

  auto drop_context = make_patch_drop_context(ctx);
  render_patch_drop_feedback(drop_context);

  auto confirmation_context = make_confirmation_context(ctx);
  render_confirmation_dialog(confirmation_context);

  auto patch_editor_context = make_patch_editor_context(ctx);
  render_patch_editor(PATCH_EDITOR_TITLE, patch_editor_context,
                      patch_editor_state());

  auto patch_selector_context = make_patch_selector_context(ctx);
  render_patch_selector(PATCH_BROWSER_TITLE, patch_selector_context);

  auto midi_keyboard_context = make_midi_keyboard_context(ctx);
  render_midi_keyboard(SOFT_KEYBOARD_TITLE, midi_keyboard_context);

  auto preferences_context = make_preferences_context(ctx);
  render_preferences_window(PREFERENCES_TITLE, preferences_context);

  auto mml_context = make_mml_console_context(ctx);
  render_mml_console(MML_CONSOLE_TITLE, mml_context);

  auto waveform_context = make_waveform_context(ctx);
  render_waveform(WAVEFORM_TITLE, waveform_context);
}

} // namespace ui
