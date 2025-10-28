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
#include "note_actions.hpp"
#include "patch_actions.hpp"
#include <filesystem>
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

MainMenuContext make_main_menu_context(AppState &app_state) {
  auto &ui_state = app_state.ui_state();
  return {app_state.history(),
          app_state.gui(),
          app_state.preference_manager(),
          ui_state.prefs,
          ui_state.open_directory_dialog,
          [&app_state]() { app_state.history().undo(app_state); },
          [&app_state]() { app_state.history().redo(app_state); }};
}

PatchDropContext make_patch_drop_context(AppState &app_state) {
  auto &ui_state = app_state.ui_state();
  return {ui_state.drop_state,
          [&app_state]() { drop_actions::cancel_selection(app_state); },
          [&app_state](size_t index) {
            drop_actions::apply_selection(app_state, index);
          }};
}

ConfirmationDialogContext make_confirmation_context(AppState &app_state) {
  auto &ui_state = app_state.ui_state();
  return {ui_state.confirmation_state, ui_state.drop_state,
          [&app_state](const patches::PatchEntry &entry) {
            patch_actions::load(app_state, entry);
          },
          [&app_state](const ym2612::Patch &patch,
                       const std::filesystem::path &path) {
            patch_actions::load_dropped_patch(app_state, patch, path);
          },
          [&app_state]() {
            app_state.gui().set_should_close(true);
            app_state.patch_session().mark_as_clean();
          }};
}

PatchEditorContext make_patch_editor_context(AppState &app_state) {
  auto &ui_state = app_state.ui_state();
  return {
      app_state.patch_session(), ui_state.prefs, ui_state.envelope_states,
      [&app_state](const std::string &label, const std::string &merge_key,
                   const ym2612::Patch &before) {
        auto label_copy = label;
        auto key_copy = merge_key;
        auto before_copy = before;
        app_state.history().begin_transaction(
            label_copy, key_copy,
            [label_copy = std::move(label_copy), key_copy = std::move(key_copy),
             before_copy = std::move(before_copy)](AppState &state) mutable {
              return history::make_snapshot_entry<ym2612::Patch>(
                  label_copy, key_copy, before_copy, state.patch(),
                  [](AppState &target, const ym2612::Patch &value) {
                    target.patch() = value;
                    target.patch_session().apply_patch_to_audio();
                  });
            });
      },
      [&app_state]() { app_state.history().commit_transaction(app_state); }};
}

PatchSelectorContext make_patch_selector_context(AppState &app_state) {
  auto &ui_state = app_state.ui_state();
  return {app_state.patch_repository(), app_state.patch_session(),
          ui_state.prefs,
          [&app_state](const patches::PatchEntry &entry) {
            patch_actions::safe_load(app_state, entry);
          },
          [](const std::filesystem::path &path) {
            reveal_in_file_manager(path.string());
          }};
}

MidiKeyboardContext make_midi_keyboard_context(AppState &app_state) {
  auto &ui_state = app_state.ui_state();
  return {ui_state.prefs,
          app_state.input_state(),
          midi_keyboard_state(),
          [&app_state](ym2612::Note note, uint8_t velocity) {
            return input::note_on(app_state, note, velocity);
          },
          [&app_state](ym2612::Note note) {
            return input::note_off(app_state, note);
          },
          [&app_state](const ym2612::Note &note) {
            return input::note_is_pressed(app_state, note);
          },
          [&app_state]() { return input::active_notes(app_state); }};
}

PreferencesContext make_preferences_context(AppState &app_state) {
  auto &ui_state = app_state.ui_state();
  return {app_state.preference_manager(),
          ui_state.prefs,
          ui_state.open_directory_dialog,
          app_state.path_service().paths(),
          app_state.connected_midi_inputs(),
          [&app_state]() {
            app_state.path_service().ensure_directories();
            app_state.patch_session().refresh_directories();
          },
          [&app_state](ui::styles::ThemeId theme_id) {
            app_state.gui().set_theme(theme_id);
          }};
}

MmlConsoleContext make_mml_console_context(AppState &app_state) {
  auto &ui_state = app_state.ui_state();
  return {ui_state.prefs, [&app_state]() -> const ym2612::Patch & {
            return app_state.patch();
          }};
}

WaveformContext make_waveform_context(AppState &app_state) {
  auto &ui_state = app_state.ui_state();
  return {
      ui_state.prefs, app_state.wave_sampler(),
      [&app_state]() -> const ym2612::Patch & { return app_state.patch(); }};
}

} // namespace

void render_all(AppState &app_state, ym2612::FFTAnalyzer &analyzer) {
  auto menu_context = make_main_menu_context(app_state);
  render_main_menu(menu_context);

  auto drop_context = make_patch_drop_context(app_state);
  render_patch_drop_feedback(drop_context);

  auto confirmation_context = make_confirmation_context(app_state);
  render_confirmation_dialog(confirmation_context);

  auto patch_editor_context = make_patch_editor_context(app_state);
  render_patch_editor(PATCH_EDITOR_TITLE, patch_editor_context,
                      patch_editor_state());

  auto patch_selector_context = make_patch_selector_context(app_state);
  render_patch_selector(PATCH_BROWSER_TITLE, patch_selector_context);

  auto midi_keyboard_context = make_midi_keyboard_context(app_state);
  render_midi_keyboard(SOFT_KEYBOARD_TITLE, midi_keyboard_context);

  auto preferences_context = make_preferences_context(app_state);
  render_preferences_window(PREFERENCES_TITLE, preferences_context);

  auto mml_context = make_mml_console_context(app_state);
  render_mml_console(MML_CONSOLE_TITLE, mml_context);

  auto waveform_context = make_waveform_context(app_state);
  render_waveform(WAVEFORM_TITLE, waveform_context, analyzer);
}

} // namespace ui
