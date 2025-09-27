#include "main_menu.hpp"

#include "../gui_manager.hpp"
#include <imgui.h>

namespace ui {

void render_main_menu(AppState &app_state) {
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("View")) {
      auto &ui_state = app_state.ui_state();

      bool fullscreen = app_state.gui_manager().is_fullscreen();
      if (ImGui::MenuItem("Fullscreen", nullptr, fullscreen)) {
        app_state.gui_manager().set_fullscreen(!fullscreen);
      }

      ImGui::Separator();

      ImGui::MenuItem("Patch Editor", nullptr, &ui_state.show_patch_editor);
      ImGui::MenuItem("Keyboard Typing", nullptr,
                      &ui_state.show_audio_controls);
      ImGui::MenuItem("Soft Keyboard", nullptr, &ui_state.show_midi_keyboard);
      ImGui::MenuItem("Patch Browser", nullptr, &ui_state.show_patch_selector);
      ImGui::MenuItem("Preferences", nullptr, &ui_state.show_preferences);

      ImGui::Separator();

      // Reset buttons
      if (ImGui::MenuItem("Reset to Default View")) {
        app_state.preference_manager().reset_ui_preferences();
        const auto &ui_prefs = app_state.preference_manager().ui_preferences();
        auto &state = app_state.ui_state();
        state.show_patch_editor = ui_prefs.show_patch_editor;
        state.show_audio_controls = ui_prefs.show_audio_controls;
        state.show_midi_keyboard = ui_prefs.show_midi_keyboard;
        state.show_patch_selector = ui_prefs.show_patch_selector;
        state.show_preferences = ui_prefs.show_preferences;
        state.patch_search_query = ui_prefs.patch_search_query;
        state.open_directory_dialog = false;
        app_state.gui_manager().reset_layout();
        app_state.gui_manager().set_theme(ui::styles::ThemeId::MegatoyDark);
      }
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }
}

} // namespace ui
