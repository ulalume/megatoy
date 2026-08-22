#include "main_menu.hpp"
#include "platform/platform_config.hpp"
#include <algorithm>
#include <filesystem>
#include <optional>

#include "about_dialog.hpp"
#include "changelog_dialog.hpp"
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
      if (ImGui::MenuItem("Change Log...")) {
        open_changelog_dialog();
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
    const bool is_patch_modified = session.is_modified();
    const bool is_user_patch = session.current_patch_is_user_patch();
    const bool save_disabled = is_user_patch && !is_patch_modified;
    if (ImGui::BeginMenu("File")) {
      const bool mac_behavior = io.ConfigMacOSXBehaviors;
      const char *save_shortcut = mac_behavior ? "Cmd+S" : "Ctrl+S";
      const char *save_label = save_label_for(session, is_user_patch);
      if (save_disabled)
        ImGui::BeginDisabled(true);
      if (ImGui::MenuItem(save_label, save_shortcut)) {
        if (is_user_patch) {
          trigger_save(session, context.save_state);
        } else {
          request_save_as(context.save_state);
        }
      }
      if (save_disabled)
        ImGui::EndDisabled();

      const char *save_as_shortcut =
          mac_behavior ? "Shift+Cmd+S" : "Shift+Ctrl+S";
      if (is_user_patch && ImGui::MenuItem("Save As...", save_as_shortcut)) {
        request_save_as(context.save_state);
      }

      {
        ImGui::Separator();
        // In the browser a folder is copied in rather than referenced, so it
        // is labelled for what it does.
        const char *label = PreferenceManager::folder_add_is_import()
                                ? "Import Folder into Workspace..."
                                : "Add Folder to Workspace...";
        if (ImGui::MenuItem(label)) {
          context.open_add_folder_dialog = true;
        }

        const auto &folders = context.preferences.workspace().folders();
        const bool has_removable_folder = std::any_of(
            folders.begin(), folders.end(), [&](const auto &folder) {
              return !context.preferences.workspace_folder_is_protected(
                  folder.path);
            });
        const char *remove_label =
            megatoy::platform::is_web() ? "Delete Folder" : "Remove Folder";
        if (ImGui::BeginMenu(remove_label, has_removable_folder)) {
          std::optional<std::filesystem::path> to_remove;
          for (const auto &folder : folders) {
            if (context.preferences.workspace_folder_is_protected(
                    folder.path)) {
              continue;
            }
            if (ImGui::MenuItem(folder.path.string().c_str())) {
              to_remove = folder.path;
            }
          }
          ImGui::EndMenu();
          if (to_remove && context.remove_workspace_folder) {
            context.remove_workspace_folder(*to_remove);
          }
        }
      }

      ImGui::EndMenu();
    }

    // The folder picker is modal, so it is opened after the menu closes.
    if (context.open_add_folder_dialog) {
      context.open_add_folder_dialog = false;
      auto sync = context.sync_workspace;
      context.preferences.request_add_workspace_folder([sync]() {
        if (sync) {
          sync();
        }
      });
    }

    // Not gated on supports_quit(): saving has nothing to do with whether the
    // platform can be quit, and on the web that gate silently took Cmd+S away.
    if (!save_disabled) {
      const bool primary_modifier = (io.KeyCtrl || io.KeySuper) && !io.KeyShift;
      if (primary_modifier && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (is_user_patch) {
          trigger_save(session, context.save_state);
        } else {
          request_save_as(context.save_state);
        }
      }
    }
    const bool save_as_modifier = (io.KeyCtrl || io.KeySuper) && io.KeyShift;
    if (save_as_modifier && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
      request_save_as(context.save_state);
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

      ImGui::Separator();

      // The items name their target the way the undo entries above do, which
      // saves the menu a section heading.
      OperatorCommandContext operator_commands{
          session.current_patch().instrument, context.operator_edit,
          context.begin_patch_history, context.commit_patch_history};
      render_operator_command_items(operator_commands);

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
      ImGui::MenuItem(SOFT_KEYBOARD_TITLE, nullptr,
                      &ui_prefs.show_midi_keyboard);
      ImGui::MenuItem(MML_CONSOLE_TITLE, nullptr, &ui_prefs.show_mml_console);
      ImGui::MenuItem(WAVEFORM_TITLE, nullptr, &ui_prefs.show_waveform);
      ImGui::MenuItem(PREFERENCES_TITLE, nullptr, &ui_prefs.show_preferences);

      ImGui::Separator();

      // Reset buttons
      if (ImGui::MenuItem("Reset to Default View")) {
        context.preferences.reset_ui_preferences();
        context.ui_prefs = context.preferences.ui_preferences();
        context.open_add_folder_dialog = false;
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
  render_changelog_dialog();
}

} // namespace ui
