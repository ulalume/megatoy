#include "ui_renderer.hpp"
#include "gui/components/confirmation_dialog.hpp"
#include "gui/components/main_menu.hpp"
#include "gui/components/midi_keyboard.hpp"
#include "gui/components/mml_console.hpp"
#include "gui/components/patch_drop.hpp"
#include "gui/components/patch_editor.hpp"
#include "gui/components/patch_selector.hpp"
#include "gui/components/preferences.hpp"
#include "gui/components/waveform.hpp"
#include "gui/window_title.hpp"
#include <imgui.h>
#include <imgui_internal.h>

namespace ui {

void render_all(AppState &app_state, ym2612::FFTAnalyzer &analyzer) {
  ui::render_main_menu(app_state);

  ui::render_patch_drop_feedback(app_state);
  ui::render_confirmation_dialog(app_state);

  ui::render_patch_editor(PATCH_EDITOR_TITLE, app_state);
  ui::render_patch_selector(PATCH_BROWSER_TITLE, app_state);

  ui::render_midi_keyboard(SOFT_KEYBOARD_TITLE, app_state);

  ui::render_preferences_window(PREFERENCES_TITLE, app_state);
  ui::render_mml_console(MML_CONSOLE_TITLE, app_state);
  ui::render_waveform(WAVEFORM_TITLE, app_state, analyzer);
}

} // namespace ui
