#include "ui_renderer.hpp"
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
#include <filesystem>
#include <imgui.h>
#include <imgui_internal.h>
#include <utility>

namespace ui {

void render_all(AppState &app_state, ym2612::FFTAnalyzer &analyzer) {
  auto &ui_state = app_state.ui_state();

  MainMenuContext main_menu_context{
      app_state.history(),
      app_state.gui(),
      app_state.preference_manager(),
      ui_state.prefs,
      ui_state.open_directory_dialog,
      [&app_state]() { app_state.history().undo(app_state); },
      [&app_state]() { app_state.history().redo(app_state); }};
  ui::render_main_menu(main_menu_context);

  PatchDropContext patch_drop_context{
      ui_state.drop_state,
      [&app_state]() { app_state.cancel_instrument_selection(); },
      [&app_state](size_t index) {
        app_state.apply_mml_instrument_selection(index);
      }};
  ui::render_patch_drop_feedback(patch_drop_context);

  ConfirmationDialogContext confirmation_context{
      ui_state.confirmation_state, ui_state.drop_state,
      [&app_state](const patches::PatchEntry &entry) {
        app_state.load_patch(entry);
      },
      [&app_state](const ym2612::Patch &patch,
                   const std::filesystem::path &path) {
        app_state.load_dropped_patch_with_history(patch, path);
      },
      [&app_state]() {
        app_state.gui().set_should_close(true);
        app_state.patch_session().mark_as_clean();
      }};
  ui::render_confirmation_dialog(confirmation_context);

  PatchEditorContext patch_editor_context{
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
                    target.apply_patch_to_device();
                  });
            });
      },
      [&app_state]() { app_state.history().commit_transaction(app_state); }};

  static PatchEditorState patch_editor_state;
  ui::render_patch_editor(PATCH_EDITOR_TITLE, patch_editor_context,
                          patch_editor_state);

  PatchSelectorContext patch_selector_context{
      app_state.patch_repository(), app_state.patch_session(), ui_state.prefs,
      [&app_state](const patches::PatchEntry &entry) {
        app_state.safe_load_patch(entry);
      },
      [](const std::filesystem::path &path) {
        reveal_in_file_manager(path.string());
      }};
  ui::render_patch_selector(PATCH_BROWSER_TITLE, patch_selector_context);

  static MidiKeyboardState midi_keyboard_state;
  MidiKeyboardContext midi_keyboard_context{
      ui_state.prefs,
      app_state.input_state(),
      midi_keyboard_state,
      [&app_state](ym2612::Note note, uint8_t velocity) {
        return app_state.key_on(note, velocity);
      },
      [&app_state](ym2612::Note note) { return app_state.key_off(note); },
      [&app_state](const ym2612::Note &note) {
        return app_state.key_is_pressed(note);
      },
      [&app_state]() { return app_state.active_notes(); }};
  ui::render_midi_keyboard(SOFT_KEYBOARD_TITLE, midi_keyboard_context);

  PreferencesContext preferences_context{
      app_state.preference_manager(),
      ui_state.prefs,
      ui_state.open_directory_dialog,
      app_state.path_service().paths(),
      app_state.connected_midi_inputs(),
      [&app_state]() { app_state.sync_patch_directories(); },
      [&app_state](ui::styles::ThemeId theme_id) {
        app_state.gui().set_theme(theme_id);
      }};
  ui::render_preferences_window(PREFERENCES_TITLE, preferences_context);

  MmlConsoleContext mml_console_context{
      ui_state.prefs,
      [&app_state]() -> const ym2612::Patch & { return app_state.patch(); }};
  ui::render_mml_console(MML_CONSOLE_TITLE, mml_console_context);

  WaveformContext waveform_context{
      ui_state.prefs, app_state.wave_sampler(),
      [&app_state]() -> const ym2612::Patch & { return app_state.patch(); }};
  ui::render_waveform(WAVEFORM_TITLE, waveform_context, analyzer);
}

} // namespace ui
