#include "ui_renderer.hpp"
#include "gui/components/keyboard_typing.hpp"
#include "gui/components/main_menu.hpp"
#include "gui/components/midi_keyboard.hpp"
#include "gui/components/mml_console.hpp"
#include "gui/components/patch_drop.hpp"
#include "gui/components/patch_editor.hpp"
#include "gui/components/patch_selector.hpp"
#include "gui/components/preferences.hpp"
#include "gui/components/waveform.hpp"
#include <imgui.h>

namespace ui {

UIRenderer::UIRenderer(AppState &app_state) : app_state_(app_state) {}

void UIRenderer::render() {
  render_main_menu();
  render_patch_components();
  render_input_components();
  render_utility_components();
  update_ui_state();
}

void UIRenderer::render_main_menu() { ui::render_main_menu(app_state_); }

void UIRenderer::render_patch_components() {
  ui::render_patch_drop_feedback(app_state_);
  ui::render_patch_editor(app_state_);
  ui::render_patch_selector(app_state_);
}

void UIRenderer::render_input_components() {
  ui::render_midi_keyboard(app_state_);
  ui::render_keyboard_typing(app_state_);
}

void UIRenderer::render_utility_components() {
  ui::render_preferences_window(app_state_);
  ui::render_mml_console(app_state_);
  ui::render_waveform(app_state_);
}

void UIRenderer::update_ui_state() {
  // Reset text input focus state after frame
  app_state_.input_state().text_input_focused = false;

  // Update preferences from UI state
  app_state_.preference_manager().set_ui_preferences(
      app_state_.ui_state().prefs);
}

} // namespace ui
