#include "preferences.hpp"
#include "../preference_manager.hpp"
#include "styles/megatoy_style.hpp"
#include "styles/theme.hpp"
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

    ImGui::Spacing();

    // Directory status indicator
    if (prefs.is_initialized()) {
      ImGui::TextColored(styles::color(styles::MegatoyCol::StatusSuccess),
                         "Directories initialized");
    } else {
      ImGui::TextColored(styles::color(styles::MegatoyCol::StatusError),
                         "Directory initialization failed");
      if (ImGui::Button("Retry Directory Creation")) {
        if (prefs.ensure_directories_exist()) {
          app_state.sync_patch_directories();
        }
      }
    }

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::Text("Theme");

    const auto &themes = ui::styles::available_themes();
    int current_theme_index = 0;
    auto current_theme = prefs.theme();
    for (int i = 0; i < static_cast<int>(themes.size()); ++i) {
      if (themes[i].id == current_theme) {
        current_theme_index = i;
        break;
      }
    }

    const char *theme_preview =
        themes.empty() ? "" : themes[current_theme_index].display_name;
    if (ImGui::BeginCombo("##UI Theme", theme_preview)) {
      for (int i = 0; i < static_cast<int>(themes.size()); ++i) {
        const bool is_selected = (i == current_theme_index);
        if (ImGui::Selectable(themes[i].display_name, is_selected)) {
          current_theme_index = i;
          auto selected_id = themes[i].id;
          prefs.set_theme(selected_id);
          app_state.gui_manager().set_theme(selected_id);
        }
        if (is_selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::Text("Preferences are automatically saved");
  }
  ImGui::End();

  if (ui_state.open_directory_dialog) {
    ui_state.open_directory_dialog = false;
    if (prefs.select_data_directory()) {
      app_state.sync_patch_directories();
    }
  }
}
} // namespace ui
