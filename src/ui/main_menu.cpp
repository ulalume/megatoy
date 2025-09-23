#include "main_menu.hpp"

#include "../gui_manager.hpp"
#include <imgui.h>

namespace ui {

void render_main_menu(AppState &app_state) {
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("View")) {
      auto &ui_state = app_state.ui_state();
      ImGui::MenuItem("Patch Editor", nullptr, &ui_state.show_patch_editor);
      ImGui::MenuItem("Keyboard Typing", nullptr,
                      &ui_state.show_audio_controls);
      ImGui::MenuItem("Soft Keyboard", nullptr, &ui_state.show_midi_keyboard);
      ImGui::MenuItem("Patch Browser", nullptr, &ui_state.show_patch_selector);
      ImGui::MenuItem("Preferences", nullptr, &ui_state.show_preferences);

      bool fullscreen = app_state.gui_manager().is_fullscreen();
      if (ImGui::MenuItem("Fullscreen", nullptr, fullscreen)) {
        app_state.gui_manager().set_fullscreen(!fullscreen);
      }
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }
}

} // namespace ui
