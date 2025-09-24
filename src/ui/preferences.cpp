#include "preferences.hpp"
#include "../preference_manager.hpp"
#include <imgui.h>

namespace ui {

void render_preferences_window(AppState &app_state) {
  auto &prefs = app_state.preference_manager();

  auto &ui_state = app_state.ui_state();
  if (!ui_state.show_preferences) {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_FirstUseEver);

  if (ImGui::Begin("Preferences", &ui_state.show_preferences)) {

    // Show the current directory
    ImGui::Text("Current data directory:");
    ImGui::TextWrapped("%s", prefs.get_data_directory().c_str());
    ImGui::Spacing();

    // Directory selection button
    if (ImGui::Button("Select Directory...")) {
      ui_state.open_directory_dialog = true;
    }

    ImGui::SameLine();

    // Reset-to-default button
    if (ImGui::Button("Reset to Default")) {
      prefs.reset_to_defaults();
      app_state.sync_patch_directories();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Sub-directory info
    ImGui::Text("Sub-directories:");
    ImGui::Bullet();
    ImGui::Text("Patches: %s", prefs.get_patches_directory().c_str());
    ImGui::Bullet();
    ImGui::Text("User Patches: %s", prefs.get_user_patches_directory().c_str());
    ImGui::Text("Projects: %s", prefs.get_projects_directory().c_str());

    ImGui::Spacing();

    // Directory status indicator
    if (prefs.is_initialized()) {
      ImGui::TextColored(ImVec4(0, 1, 0, 1), "Directories initialized");
    } else {
      ImGui::TextColored(ImVec4(1, 0, 0, 1), "Directory initialization failed");
      if (ImGui::Button("Retry Directory Creation")) {
        if (prefs.ensure_directories_exist()) {
          app_state.sync_patch_directories();
        }
      }
    }

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::Text("Preferences are automatically saved");
  }
  ImGui::End();

}
} // namespace ui
