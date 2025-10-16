#include "patch_editor.hpp"
#include "patches/patch_manager.hpp"
#include "patches/patch_repository.hpp"
#include "history_helpers.hpp"
#include "operator_editor.hpp"
#include "gui/components/preview/algorithm_preview.hpp"
#include <cctype>
#include <cstring>
#include <imgui.h>

#include "gui/styles/megatoy_style.hpp"

namespace ui {
void center_next_window() {
  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
}
void force_center_window() {
  ImVec2 pos = ImGui::GetWindowPos();
  ImVec2 size = ImGui::GetWindowSize();
  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImVec2 target_pos(center.x - size.x * 0.5f, center.y - size.y * 0.5f);

  if (abs(pos.x - target_pos.x) > 5.0f || abs(pos.y - target_pos.y) > 5.0f) {
    ImGui::SetWindowPos(target_pos, ImGuiCond_Always);
  }
}

// ImGui callback to block invalid characters
static int filename_input_callback(ImGuiInputTextCallbackData *data) {
  if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter) {
    if (!patches::is_valid_filename_char(data->EventChar)) {
      return 1; // Reject the character
    }
  }
  return 0;
}

namespace {

bool is_patch_name_valid(const ym2612::Patch &patch) {
  return !patch.name.empty() &&
         patches::sanitize_filename(patch.name) == patch.name;
}

void render_save_export_buttons(AppState &app_state, bool name_valid,
                                UIState::PatchEditorState &state) {
  auto &patch_manager = app_state.patch_manager();
  auto &repository = app_state.patch_repository();

  if (!name_valid) {
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Button("Save");
    ImGui::SameLine();
    ImGui::Button("Export...");
    ImGui::PopStyleVar();
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Enter a valid patch name to save");
    }
    return;
  }

  if (ImGui::Button("Save")) {
    auto result = patch_manager.save_current_patch(false);
    if (result.is_duplicated()) {
      state.last_export_path = result.path.string();
      ImGui::OpenPopup("Overwrite Confirmation");
      patch_manager.set_current_patch_path(
          repository.to_relative_path(result.path));
    } else if (result.is_success()) {
      state.last_export_path = result.path.string();
      ImGui::OpenPopup("Save Success");
      repository.refresh();
      patch_manager.set_current_patch_path(
          repository.to_relative_path(result.path));
    } else if (result.is_error()) {
      state.last_export_error = result.error_message;
      ImGui::OpenPopup("Error##SaveOrExport");
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Export...")) {
    ImGui::OpenPopup("Export Options");
  }
}

void render_patch_name_field(AppState &app_state, ym2612::Patch &patch,
                             bool name_valid) {
  ImGui::PushItemWidth(200);
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
  ImGui::PopItemWidth();
}

void render_save_export_popups(AppState &app_state, const ym2612::Patch &patch,
                               UIState::PatchEditorState &state) {
  center_next_window();
  if (ImGui::BeginPopupModal("Overwrite Confirmation", nullptr,
                             ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
    force_center_window();
    ImGui::Text("A patch with this name already exists:");
    ImGui::Text("\"%s\"", patch.name.c_str());
    ImGui::Spacing();
    ImGui::Text("Do you want to overwrite it?");
    ImGui::Spacing();

    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    const bool overwrite_button = ImGui::Button("Overwrite", ImVec2(120, 0));

    ImGui::EndPopup();

    if (overwrite_button) {
      auto result = app_state.patch_manager().save_current_patch(true);
      if (result.is_success()) {
        state.last_export_path = result.path.string();
        ImGui::OpenPopup("Save Success");
      } else if (result.is_error()) {
        state.last_export_error = result.error_message;
        ImGui::OpenPopup("Error##SaveOrExport");
      }
      ImGui::CloseCurrentPopup();
    }
  }

  center_next_window();
  if (ImGui::BeginPopupModal("Save Success", nullptr,
                             ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
    force_center_window();
    ImGui::Text("Patch saved successfully.");
    ImGui::TextWrapped("%s", state.last_export_path.c_str());
    ImGui::Spacing();
    if (ImGui::Button("OK", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  center_next_window();
  if (ImGui::BeginPopupModal("Export Success", nullptr,
                             ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
    force_center_window();
    ImGui::Text("Patch exported successfully.");
    ImGui::TextWrapped("%s", state.last_export_path.c_str());
    ImGui::Spacing();
    if (ImGui::Button("OK", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  center_next_window();
  if (ImGui::BeginPopupModal("Error##SaveOrExport", nullptr,
                             ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
    force_center_window();
    ImGui::TextWrapped("%s", state.last_export_error.c_str());
    ImGui::Spacing();
    if (ImGui::Button("OK", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void render_patch_metadata(AppState &app_state, ym2612::Patch &patch,
                           UIState::PatchEditorState &state) {
  const bool name_valid = is_patch_name_valid(patch);

  render_save_export_buttons(app_state, name_valid, state);

  bool export_mml = false;
  bool export_dmp = false;
  if (ImGui::BeginPopup("Export Options")) {
    export_mml = ImGui::MenuItem(".mml (ctrmml)");
    export_dmp = ImGui::MenuItem(".dmp");
    ImGui::EndPopup();
  }

  if (export_mml || export_dmp) {
    const auto export_format =
        export_mml ? patches::ExportFormat::MML : patches::ExportFormat::DMP;
    auto result =
        app_state.patch_manager().export_current_patch_as(export_format);
    if (result.is_success()) {
      state.last_export_path = result.path.string();
      ImGui::OpenPopup("Export Success");
    } else if (result.is_error()) {
      state.last_export_error = result.error_message;
      ImGui::OpenPopup("Error##SaveOrExport");
    }
  }

  render_patch_name_field(app_state, patch, name_valid);

  render_save_export_popups(app_state, patch, state);
}

void render_lfo_section(AppState &app_state, ym2612::Patch &patch,
                        bool &settings_changed) {
  ImGui::SeparatorText("Low Frequency Oscillator");
  bool lfo_enable = patch.global.lfo_enable;
  if (ImGui::Checkbox("LFO Enable", &lfo_enable)) {
    patch.global.lfo_enable = lfo_enable;
    settings_changed = true;
  }
  track_patch_history(app_state, "LFO Enable", "global.lfo_enable");

  ImGui::PushItemWidth(hslider_width);

  int lfo_freq = patch.global.lfo_frequency;

  if (!lfo_enable)
    ImGui::BeginDisabled(true);
  bool lfo_freq_changed = ImGui::SliderInt("LFO Frequency", &lfo_freq, 0, 7);

  track_patch_history(app_state, "LFO Frequency", "global.lfo_frequency");
  if (lfo_freq_changed) {
    patch.global.lfo_frequency = static_cast<uint8_t>(lfo_freq);
    settings_changed = true;
  }

  ImGui::Spacing();

  int ams = patch.channel.amplitude_modulation_sensitivity;
  bool ams_changed =
      ImGui::SliderInt("Amplitude Modulation Sensitivity", &ams, 0, 3);
  track_patch_history(app_state, "Amplitude Modulation Sensitivity",
                      "channel.am_sensitivity");
  if (ams_changed) {
    patch.channel.amplitude_modulation_sensitivity = static_cast<uint8_t>(ams);
    settings_changed = true;
  }

  int fms = patch.channel.frequency_modulation_sensitivity;
  bool fms_changed =
      ImGui::SliderInt("Frequency Modulation Sensitivity", &fms, 0, 7);
  track_patch_history(app_state, "Frequency Modulation Sensitivity",
                      "channel.fm_sensitivity");
  if (fms_changed) {
    patch.channel.frequency_modulation_sensitivity = static_cast<uint8_t>(fms);
    settings_changed = true;
  }
  if (!lfo_enable)
    ImGui::EndDisabled();

  ImGui::PopItemWidth();
  ImGui::Spacing();
}

void render_channel_section(AppState &app_state, ym2612::Patch &patch,
                            bool &settings_changed) {
  ImGui::SeparatorText("Channel");
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

  ImGui::PushItemWidth(hslider_width);

  if (const auto *preview =
          get_algorithm_preview_texture(patch.instrument.algorithm)) {
    ImGui::Image(preview->texture_id, preview->size);
  }

  int algorithm = patch.instrument.algorithm;
  bool algorithm_changed = ImGui::SliderInt("Algorithm", &algorithm, 0, 7);
  track_patch_history(app_state, "Algorithm", "instrument.algorithm");
  if (algorithm_changed) {
    patch.instrument.algorithm = static_cast<uint8_t>(algorithm);
    settings_changed = true;
  }
  ImGui::PopItemWidth();

  ImGui::Spacing();
}

void render_operator_section(AppState &app_state, ym2612::Patch &patch,
                             bool &settings_changed) {
  ImGui::Columns(2, "operation_columns", false);
  for (auto i = 0; i < 4; i++) {
    auto op_index = static_cast<int>(ym2612::all_operator_indices[i]);

    settings_changed |= render_operator_editor(
        app_state, patch.instrument.operators[op_index], i);

    ImGui::Spacing();
    ImGui::NextColumn();
  }
  ImGui::Columns(1);
}

} // namespace

// Function to render instrument settings panel
void render_patch_editor(AppState &app_state) {
  auto &patch = app_state.patch();
  auto &ui_state = app_state.ui_state();
  auto &editor_state = ui_state.patch_editor;

  if (!ui_state.prefs.show_patch_editor) {
    return;
  }

  ImGui::SetNextWindowPos(ImVec2(400, 50), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(400, 600), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("Patch Editor", &ui_state.prefs.show_patch_editor)) {
    ImGui::End();
    return;
  }

  bool settings_changed = false;

  render_patch_metadata(app_state, patch, editor_state);
  ImGui::Spacing();

  render_lfo_section(app_state, patch, settings_changed);
  render_channel_section(app_state, patch, settings_changed);
  render_operator_section(app_state, patch, settings_changed);

  // Apply settings if changed
  if (settings_changed) {
    app_state.update_all_settings();
  }

  ImGui::End();
}

} // namespace ui
