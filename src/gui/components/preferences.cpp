#include "preferences.hpp"
#include "preferences/preference_manager.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/styles/theme.hpp"
#include <imgui.h>

namespace ui {

void render_preferences_window(AppState &app_state) {
  auto &prefs = app_state.preference_manager();
  const auto &paths = app_state.directory_service().paths();

  auto &ui_state = app_state.ui_state();
  if (!ui_state.prefs.show_preferences) {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(480, 260), ImGuiCond_FirstUseEver);

  if (ImGui::Begin("Preferences", &ui_state.prefs.show_preferences)) {
    auto &ui_prefs = ui_state.prefs;

    // Show the current directory
    ImGui::SeparatorText("Data Directory");
    ImGui::TextWrapped("%s", paths.data_root.c_str());
    ImGui::Spacing();

    if (ImGui::Button("Select Directory...")) {
      ui_state.open_directory_dialog = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("Reset to Default")) {
      prefs.reset_data_directory();
      app_state.sync_patch_directories();
    }

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

    ImGui::SeparatorText("Theme");

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
          app_state.gui().manager().set_theme(selected_id);
        }
        if (is_selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    ImGui::SeparatorText("MIDI Input");

    ImGui::Checkbox("Use MIDI velocity", &ui_prefs.use_velocity);
    if (!ui_prefs.use_velocity) {
      ImGui::TextWrapped("Notes play at full velocity.");
    }

    ImGui::Checkbox("Steal oldest note when all 6 channels are busy",
                    &ui_prefs.steal_oldest_note_when_full);

    const auto &devices = app_state.connected_midi_inputs();
    if (devices.empty()) {
      ImGui::TextUnformatted("No MIDI devices detected.");
    } else {
      ImGui::Text("Connected devices (%zu)", devices.size());
      ImGui::Indent();
      for (const auto &name : devices) {
        ImGui::BulletText("%s", name.c_str());
      }
      ImGui::Unindent();
    }
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
