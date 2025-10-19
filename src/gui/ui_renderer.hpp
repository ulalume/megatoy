#pragma once

#include "app_state.hpp"

namespace ui {

/**
 * UIRenderer - Pure UI rendering system
 */
class UIRenderer {
public:
  explicit UIRenderer(AppState &app_state);

  /**
   * Render all UI components
   * Called between begin_frame() and end_frame() by the Application
   */
  void render();

private:
  AppState &app_state_;

  // UI rendering methods organized by functionality
  void render_main_menu();
  void render_patch_components();
  void render_input_components();
  void render_utility_components();

  // Post-render state management
  void update_ui_state();
};

} // namespace ui
