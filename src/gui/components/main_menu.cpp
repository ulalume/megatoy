#include "main_menu.hpp"

#include "about_dialog.hpp"
#include "gui/window_title.hpp"
#include <imgui.h>
#include <string>
#include <string_view>
#include <sys/fcntl.h>

namespace ui {

void render_main_menu(MainMenuContext &context) {
  bool open_about = false;
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("megatoy")) {
      if (ImGui::MenuItem("About megatoy")) {
        open_about = true;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Quit")) {
        context.gui.set_should_close(true);
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
      auto &history = context.history;
      const ImGuiIO &io = ImGui::GetIO();
      const bool mac_behavior = io.ConfigMacOSXBehaviors;
      const char *undo_shortcut = mac_behavior ? "Cmd+Z" : "Ctrl+Z";
      const char *redo_shortcut = mac_behavior ? "Cmd+Shift+Z" : "Ctrl+Shift+Z";

      std::string undo_label = "Undo";
      if (history.can_undo()) {
        std::string_view change = history.undo_label();
        if (!change.empty()) {
          undo_label.append(" ");
          undo_label.append(change);
        }
      }

      if (ImGui::MenuItem(undo_label.c_str(), undo_shortcut, false,
                          history.can_undo())) {
        if (context.undo) {
          context.undo();
        }
      }

      std::string redo_label = "Redo";
      if (history.can_redo()) {
        std::string_view change = history.redo_label();
        if (!change.empty()) {
          redo_label.append(" ");
          redo_label.append(change);
        }
      }

      if (ImGui::MenuItem(redo_label.c_str(), redo_shortcut, false,
                          history.can_redo())) {
        if (context.redo) {
          context.redo();
        }
      }

      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
      auto &ui_prefs = context.ui_prefs;

      bool fullscreen = context.gui.is_fullscreen();
      if (ImGui::MenuItem("Fullscreen", nullptr, fullscreen)) {
        context.gui.set_fullscreen(!fullscreen);
      }

      ImGui::Separator();

      ImGui::MenuItem(PATCH_EDITOR_TITLE, nullptr, &ui_prefs.show_patch_editor);
      ImGui::MenuItem(SOFT_KEYBOARD_TITLE, nullptr,
                      &ui_prefs.show_midi_keyboard);
      ImGui::MenuItem(PATCH_BROWSER_TITLE, nullptr,
                      &ui_prefs.show_patch_selector);
      ImGui::MenuItem(PATCH_LAB_TITLE, nullptr, &ui_prefs.show_patch_lab);
      ImGui::MenuItem(WAVEFORM_TITLE, nullptr, &ui_prefs.show_waveform);
      ImGui::MenuItem(MML_CONSOLE_TITLE, nullptr, &ui_prefs.show_mml_console);
      ImGui::MenuItem(PREFERENCES_TITLE, nullptr, &ui_prefs.show_preferences);

      ImGui::Separator();

      // Reset buttons
      if (ImGui::MenuItem("Reset to Default View")) {
        context.preferences.reset_ui_preferences();
        context.ui_prefs = context.preferences.ui_preferences();
        context.open_directory_dialog = false;
        context.gui.reset_layout();
        context.gui.set_theme(ui::styles::ThemeId::MegatoyDark);
      }
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }

  if (open_about) {
    open_about_dialog();
  }
  render_about_dialog();
}

} // namespace ui
