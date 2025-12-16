#include "main_menu.hpp"

#include "about_dialog.hpp"
#include "gui/save_export_actions.hpp"
#include "gui/window_title.hpp"
#include <imgui.h>
#include <string>
#include <string_view>

namespace ui {

void render_main_menu(MainMenuContext &context) {
  bool open_about = false;
  const ImGuiIO &io = ImGui::GetIO();
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("megatoy")) {
      const bool mac_behavior = io.ConfigMacOSXBehaviors;

      if (ImGui::MenuItem("About megatoy")) {
        open_about = true;
      }
      if (context.gui.supports_quit()) {
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", mac_behavior ? "Cmd+Q" : "Alt+F4")) {
          context.gui.set_should_close(true);
        }
      }
      ImGui::EndMenu();
    }
    auto &session = context.patch_session;
    const bool name_valid = is_patch_name_valid(session.current_patch());
    const bool is_patch_modified = session.is_modified();
    const bool is_user_patch = session.current_patch_is_user_patch();
    const bool save_disabled =
        !name_valid || (is_user_patch && !is_patch_modified);
    if (ImGui::BeginMenu("File")) {
      const bool mac_behavior = io.ConfigMacOSXBehaviors;
      const char *save_shortcut = mac_behavior ? "Cmd+S" : "Ctrl+S";
      const char *save_label = save_label_for(session, is_user_patch);
      if (save_disabled)
        ImGui::BeginDisabled(true);
      if (ImGui::MenuItem(save_label, save_shortcut)) {
        trigger_save(session, context.save_state, is_user_patch);
      }
      if (save_disabled)
        ImGui::EndDisabled();

      if (!name_valid)
        ImGui::BeginDisabled(true);
      const char *duplicate_shortcut =
          mac_behavior ? "Shift+Cmd+S" : "Shift+Ctrl+S";
      if (ImGui::MenuItem("Duplicate...", duplicate_shortcut)) {
        start_duplicate_dialog(session, context.save_state);
      }
      if (!name_valid)
        ImGui::EndDisabled();

      ImGui::Separator();

      if (!name_valid)
        ImGui::BeginDisabled(true);
      if (ImGui::BeginMenu("Export")) {
        bool any_export = false;
        for (const auto &fmt : session.export_formats()) {
          any_export = true;
          std::string label = fmt.label.empty() ? fmt.extension : fmt.label;
          if (!fmt.extension.empty()) {
            label += " (" + fmt.extension + ")";
          }
          if (ImGui::MenuItem(label.c_str())) {
            trigger_export(session, context.save_state, fmt);
          }
        }
        if (!any_export) {
          ImGui::MenuItem("No export formats available", nullptr, false, false);
        }
        ImGui::EndMenu();
      }
      if (!name_valid)
        ImGui::EndDisabled();

      ImGui::EndMenu();
    }

    if (context.gui.supports_quit() && !save_disabled) {
      const bool primary_modifier = (io.KeyCtrl || io.KeySuper) && !io.KeyShift;
      if (primary_modifier && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        trigger_save(session, context.save_state, is_user_patch);
      }
    }
    if (name_valid) {
      const bool primary_modifier = (io.KeyCtrl || io.KeySuper) && io.KeyShift;
      if (primary_modifier && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        start_duplicate_dialog(session, context.save_state);
      }
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

      if (context.gui.supports_fullscreen()) {
        bool fullscreen = context.gui.is_fullscreen();
        if (ImGui::MenuItem("Fullscreen", nullptr, fullscreen)) {
          context.gui.set_fullscreen(!fullscreen);
        }
        ImGui::Separator();
      }

      ImGui::MenuItem(PATCH_BROWSER_TITLE, nullptr,
                      &ui_prefs.show_patch_selector);
      ImGui::MenuItem(PATCH_EDITOR_TITLE, nullptr, &ui_prefs.show_patch_editor);
      ImGui::MenuItem(PATCH_LAB_TITLE, nullptr, &ui_prefs.show_patch_lab);
      if (context.gui.supports_patch_history()) {
        ImGui::MenuItem(PATCH_HISTORY_TITLE, nullptr,
                        &ui_prefs.show_patch_history);
      }
      ImGui::MenuItem(SOFT_KEYBOARD_TITLE, nullptr,
                      &ui_prefs.show_midi_keyboard);
      ImGui::MenuItem(MML_CONSOLE_TITLE, nullptr, &ui_prefs.show_mml_console);
      if (context.gui.supports_waveform()) {
        ImGui::MenuItem(WAVEFORM_TITLE, nullptr, &ui_prefs.show_waveform);
      }
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
