#include "patch_editor.hpp"
#include "../formats/ctrmml.hpp"
#include "../formats/dmp.hpp"
#include "../formats/gin.hpp"
#include "../patches/patch_repository.hpp"
#include "../platform/file_dialog.hpp"
#include "history_helpers.hpp"
#include "operator_editor.hpp"
#include "preview/algorithm_preview.hpp"
#include <cctype>
#include <cstring>
#include <filesystem>
#include <imgui.h>

#include "styles/megatoy_style.hpp"

namespace ui {

// Check whether a character is valid in filenames
bool is_valid_filename_char(char c) {
  // Characters disallowed on Windows/Mac/Linux
  const char invalid_chars[] = {'<', '>', ':', '"', '/', '\\', '|', '?', '*'};

  // Control characters are also rejected
  if (c < 32 || c == 127) {
    return false;
  }

  // Check the invalid character list
  for (char invalid : invalid_chars) {
    if (c == invalid) {
      return false;
    }
  }

  return true;
}

// Normalise a filename by removing invalid characters
std::string sanitize_filename(const std::string &input) {
  std::string result;
  for (char c : input) {
    if (is_valid_filename_char(c)) {
      result += c;
    }
  }

  // Trim leading/trailing spaces and periods
  while (!result.empty() && (result.front() == ' ' || result.front() == '.')) {
    result.erase(0, 1);
  }
  while (!result.empty() && (result.back() == ' ' || result.back() == '.')) {
    result.pop_back();
  }

  return result;
}

// ImGui callback to block invalid characters
static int filename_input_callback(ImGuiInputTextCallbackData *data) {
  if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter) {
    if (!is_valid_filename_char(data->EventChar)) {
      return 1; // Reject the character
    }
  }
  return 0;
}

// Function to render instrument settings panel
void render_patch_editor(AppState &app_state) {
  auto &patch = app_state.patch();
  auto &ui_state = app_state.ui_state();
  static std::string last_export_path;
  static std::string last_export_error;

  if (!ui_state.prefs.show_patch_editor) {
    return;
  }

  ImGui::SetNextWindowPos(ImVec2(400, 50), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(400, 600), ImGuiCond_FirstUseEver);

  if (ImGui::Begin("Patch Editor", &ui_state.prefs.show_patch_editor)) {
    bool settings_changed = false;
    ImGui::PushItemWidth(150);

    // Save/Load section
    // Warn when the filename is empty or invalid
    bool name_valid = !patch.name.empty() &&
                      sanitize_filename(patch.name) == patch.name &&
                      !patch.name.empty();

    // Save button
    if (!name_valid) {
      ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
      ImGui::Button("Save");
      ImGui::SameLine();
      ImGui::Button("Export...");
      ImGui::PopStyleVar();
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enter a valid patch name to save");
      }
    } else {
      if (ImGui::Button("Save")) {
        // Check whether the file already exists
        auto patches_dir =
            app_state.preference_manager().get_user_patches_directory();
        auto patch_path =
            ym2612::formats::gin::build_patch_path(patches_dir, patch.name);

        if (std::filesystem::exists(patch_path)) {
          // Prompt if the file already exists
          ImGui::OpenPopup("Overwrite Confirmation");
          app_state.update_current_patch_path(
              app_state.patch_repository().to_relative_path(patch_path));
        } else {
          // Save as new file
          if (ym2612::formats::gin::save_patch(patches_dir, patch,
                                               patch.name)) {
            ImGui::OpenPopup("Save Success");
            app_state.patch_repository().refresh();
            app_state.update_current_patch_path(
                app_state.patch_repository().to_relative_path(patch_path));
          } else {
            ImGui::OpenPopup("Save Error");
          }
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Export...")) {
        ImGui::OpenPopup("Export Options");
      }
    }

    // Export Options popup
    if (ImGui::BeginPopup("Export Options")) {
      const auto default_dir =
          app_state.preference_manager().get_export_directory();
      const std::string sanitized_name =
          sanitize_filename(patch.name.empty() ? "patch" : patch.name);

      if (ImGui::MenuItem(".mml (ctrmml)")) {
        std::filesystem::path selected_path;
        std::string default_filename =
            sanitized_name.empty() ? "patch.mml" : sanitized_name + ".mml";
        auto result = platform::file_dialog::save_file(
            default_dir, default_filename,
            {{"ctrmml text", {"txt"}}, {"MML", {"mml"}}}, selected_path);
        if (result == platform::file_dialog::DialogResult::Ok) {
          if (selected_path.extension().empty()) {
            selected_path.replace_extension(".mml");
          }
          if (ym2612::formats::ctrmml::write_patch(patch, selected_path)) {
            last_export_path = selected_path.string();
            ImGui::OpenPopup("Export Success");
          } else {
            last_export_error = "Failed to export to " + selected_path.string();
            ImGui::OpenPopup("Export Error");
          }
        } else if (result == platform::file_dialog::DialogResult::Error) {
          last_export_error = "Could not open save dialog.";
          ImGui::OpenPopup("Export Error");
        }
      }
      if (ImGui::MenuItem(".dmp")) {
        std::filesystem::path selected_path;
        std::string default_filename =
            sanitized_name.empty() ? "patch.dmp" : sanitized_name + ".dmp";
        auto result = platform::file_dialog::save_file(
            default_dir, default_filename, {{"DefleMask preset", {"dmp"}}},
            selected_path);
        if (result == platform::file_dialog::DialogResult::Ok) {
          if (selected_path.extension().empty()) {
            selected_path.replace_extension(".dmp");
          }
          if (ym2612::formats::dmp::write_patch(patch, selected_path)) {
            last_export_path = selected_path.string();
            ImGui::OpenPopup("Export Success");
          } else {
            last_export_error = "Failed to export to " + selected_path.string();
            ImGui::OpenPopup("Export Error");
          }
        } else if (result == platform::file_dialog::DialogResult::Error) {
          last_export_error = "Could not open save dialog.";
          ImGui::OpenPopup("Export Error");
        }
      }
      ImGui::EndPopup();
    }

    // Patch name input with filename validation
    char name_buffer[64];
    std::strncpy(name_buffer, patch.name.c_str(), sizeof(name_buffer) - 1);
    name_buffer[sizeof(name_buffer) - 1] = '\0';

    if (ImGui::InputText("Name", name_buffer, sizeof(name_buffer),
                         ImGuiInputTextFlags_CallbackCharFilter,
                         filename_input_callback)) {
      patch.name = std::string(name_buffer);
    }
    track_patch_history(app_state, "Patch Name", "meta.name");
    if (ImGui::IsItemActive()) {
      app_state.input_state().text_input_focused = true;
    }

    if (!name_valid && !patch.name.empty()) {
      ImGui::SameLine();
      ImGui::TextColored(styles::color(styles::MegatoyCol::StatusWarning),
                         "Invalid filename");
    }

    // Duplicate confirmation dialog
    if (ImGui::BeginPopupModal("Overwrite Confirmation", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("A patch with this name already exists:");
      ImGui::Text("\"%s\"", patch.name.c_str());
      ImGui::Spacing();
      ImGui::Text("Do you want to overwrite it?");
      ImGui::Spacing();

      if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
      }

      ImGui::SameLine();

      if (ImGui::Button("Overwrite", ImVec2(120, 0))) {
        auto patches_dir =
            app_state.preference_manager().get_user_patches_directory();
        if (ym2612::formats::gin::save_patch(patches_dir, patch, patch.name)) {
          ImGui::OpenPopup("Save Success");
        } else {
          ImGui::OpenPopup("Save Error");
        }
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }

    // Save-success dialog
    if (ImGui::BeginPopupModal("Save Success", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Patch saved successfully!");
      ImGui::Text("File: %s.gin", patch.name.c_str());
      ImGui::Spacing();

      if (ImGui::Button("OK", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }

    // Save-error dialog
    if (ImGui::BeginPopupModal("Save Error", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Failed to save patch!");
      ImGui::Text("Please check directory permissions.");
      ImGui::Spacing();

      if (ImGui::Button("OK", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Export Success", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Export completed successfully.");
      ImGui::TextWrapped("%s", last_export_path.c_str());
      ImGui::Spacing();
      if (ImGui::Button("OK", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Export Error", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Export failed.");
      ImGui::TextWrapped("%s", last_export_error.c_str());
      ImGui::Spacing();
      if (ImGui::Button("OK", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    // Category input
    char category_buffer[32];
    std::strncpy(category_buffer, patch.category.c_str(),
                 sizeof(category_buffer) - 1);
    category_buffer[sizeof(category_buffer) - 1] = '\0';
    if (ImGui::InputText("Category", category_buffer,
                         sizeof(category_buffer))) {
      patch.category = std::string(category_buffer);
    }
    track_patch_history(app_state, "Patch Category", "meta.category");
    if (ImGui::IsItemActive()) {
      app_state.input_state().text_input_focused = true;
    }

    // Global Settings
    //
    ImGui::SeparatorText("Global Register");
    bool lfo_enable = patch.global.lfo_enable;
    if (ImGui::Checkbox("LFO Enable", &lfo_enable)) {
      patch.global.lfo_enable = lfo_enable;
      settings_changed = true;
    }
    track_patch_history(app_state, "LFO Enable", "global.lfo_enable");

    int lfo_freq = patch.global.lfo_frequency;
    bool lfo_freq_changed = ImGui::SliderInt("LFO Frequency", &lfo_freq, 0, 7);
    track_patch_history(app_state, "LFO Frequency", "global.lfo_frequency");
    if (lfo_freq_changed) {
      patch.global.lfo_frequency = static_cast<uint8_t>(lfo_freq);
      settings_changed = true;
    }

    // Channel Settings

    ImGui::SeparatorText("Channel Register");

    ImGui::Columns(2, "channel_columns", false);
    ImGui::PushItemWidth(150);
    bool left_speaker = patch.channel.left_speaker;
    if (ImGui::Checkbox("Left Speaker", &left_speaker)) {
      patch.channel.left_speaker = left_speaker;
      settings_changed = true;
    }
    track_patch_history(app_state, "Left Speaker", "channel.left_speaker");

    ImGui::SameLine();

    bool right_speaker = patch.channel.right_speaker;
    if (ImGui::Checkbox("Right Speaker", &right_speaker)) {
      patch.channel.right_speaker = right_speaker;
      settings_changed = true;
    }
    track_patch_history(app_state, "Right Speaker", "channel.right_speaker");

    int ams = patch.channel.amplitude_modulation_sensitivity;
    bool ams_changed =
        ImGui::SliderInt("Amplitude Modulation Sensitivity", &ams, 0, 3);
    track_patch_history(app_state, "Amplitude Modulation Sensitivity",
                        "channel.am_sensitivity");
    if (ams_changed) {
      patch.channel.amplitude_modulation_sensitivity =
          static_cast<uint8_t>(ams);
      settings_changed = true;
    }

    int fms = patch.channel.frequency_modulation_sensitivity;
    bool fms_changed =
        ImGui::SliderInt("Frequency Modulation Sensitivity", &fms, 0, 7);
    track_patch_history(app_state, "Frequency Modulation Sensitivity",
                        "channel.fm_sensitivity");
    if (fms_changed) {
      patch.channel.frequency_modulation_sensitivity =
          static_cast<uint8_t>(fms);
      settings_changed = true;
    }

    ImGui::Spacing();

    // Feedback (0-7)
    int feedback = patch.instrument.feedback;
    bool feedback_changed =
        ImGui::SliderInt("Operator 1 Feedback", &feedback, 0, 7);
    track_patch_history(app_state, "Operator 1 Feedback",
                        "instrument.feedback");
    if (feedback_changed) {
      patch.instrument.feedback = static_cast<uint8_t>(feedback);
      settings_changed = true;
    }

    ImGui::NextColumn();

    if (const auto *preview =
            get_algorithm_preview_texture(patch.instrument.algorithm)) {
      ImGui::Image(preview->texture_id, preview->size);
    }
    // Algorithm (0-7)
    int algorithm = patch.instrument.algorithm;
    bool algorithm_changed = ImGui::SliderInt("Algorithm", &algorithm, 0, 7);
    track_patch_history(app_state, "Algorithm", "instrument.algorithm");
    if (algorithm_changed) {
      patch.instrument.algorithm = static_cast<uint8_t>(algorithm);
      settings_changed = true;
    }

    ImGui::PopItemWidth();

    ImGui::Columns(1);

    // Operators
    ImGui::Columns(2, "operation_columns", false);
    for (auto i = 0; i < 4; i++) {
      auto op_index = static_cast<int>(ym2612::all_operator_indices[i]);
      ym2612::OperatorSettings old_op = patch.instrument.operators[op_index];
      render_operator_editor(app_state, patch.instrument.operators[op_index],
                             i + 1);

      if (old_op != patch.instrument.operators[op_index]) {
        settings_changed = true;
      }

      ImGui::NextColumn();
    }
    ImGui::Columns(1);

    // Apply settings if changed
    if (settings_changed) {
      app_state.update_all_settings();
    }
    ImGui::PopItemWidth();
  }

  ImGui::End();
}

} // namespace ui
