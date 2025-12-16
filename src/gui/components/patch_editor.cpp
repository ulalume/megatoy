#include "patch_editor.hpp"
#include "common.hpp"
#include "gui/components/preview/algorithm_preview.hpp"
#include "gui/save_export_actions.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "operator_editor.hpp"
#include "platform/platform_config.hpp"
#include <cctype>
#include <cstring>
#include <filesystem>
#include <optional>
#include <imgui.h>

namespace ui {

void track_patch_history(PatchEditorContext &context, const std::string &label,
                         const std::string &merge_key) {
  const std::string key = merge_key.empty() ? label : merge_key;
  if (ImGui::IsItemActivated()) {
    auto before = context.session.current_patch();
    if (context.begin_history) {
      context.begin_history(label, key, before);
    }
  }
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    if (context.commit_history) {
      context.commit_history();
    }
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

void render_save_export_buttons(PatchEditorContext &context, bool name_valid,
                                PatchEditorState &state) {
  auto &patch_session = context.session;

  auto is_user_patch = patch_session.current_patch_is_user_patch();
  auto is_patch_modified = patch_session.is_modified();

  auto save_button_is_disabled =
      !name_valid || (is_user_patch && !is_patch_modified);

  if (save_button_is_disabled) {
    ImGui::BeginDisabled(true);
  }

  const char *save_label = save_label_for(patch_session, is_user_patch);
  ImVec2 pos = ImGui::GetCursorPos();
  if (ImGui::Button(save_label)) {
    trigger_save(patch_session, state, is_user_patch);
  }

  // for hover
  if (save_button_is_disabled) {
    ImGui::SetCursorPos(pos);
    ImGui::EndDisabled();
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
    std::string dummy_label = std::string(save_label) + "##dummy";
    ImGui::Button(dummy_label.c_str());
    ImGui::PopStyleVar();
    ImGui::BeginDisabled(true);
  }
  if (ImGui::IsItemHovered()) {
    if (!name_valid) {
      ImGui::SetTooltip("Enter a valid patch name to save");
    } else if (!is_user_patch) {
      const auto &path = patch_session.current_patch_path();
      if (path.ends_with(".ginpkg")) {
        ImGui::SetTooltip("Save version to %s", path.c_str());
      } else {
        ImGui::SetTooltip("Save to %s/%s.ginpkg",
                          patch_session.repository().primary_writable_label().c_str(),
                          patch_session.current_patch().name.c_str());
      }
    } else if (!is_patch_modified) {
      ImGui::SetTooltip("Patch is not modified");
    }
  }
  if (save_button_is_disabled) {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  if (!name_valid) {
    ImGui::BeginDisabled(true);
  }
  if (ImGui::Button("Duplicate...")) {
    start_duplicate_dialog(context.session, state);
  }
  if (!name_valid) {
    ImGui::EndDisabled();
  }

  if (!name_valid) {
    ImGui::BeginDisabled(true);
  }
  ImGui::SameLine();
  if (ImGui::Button("Export...")) {
    ImGui::OpenPopup("Export Options");
  }
  if (!name_valid) {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  auto relative_path = patch_session.repository().to_relative_path(
      patch_session.current_patch_path());
  ImGui::Text("%s", display_preset_path(relative_path).c_str());

  // Render popups in the same window/ID stack as the actions that open them.
  render_save_export_popups(patch_session, state);
  render_duplicate_dialog(patch_session, state);
}

void render_patch_name_field(PatchEditorContext &context, ym2612::Patch &patch,
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

  track_patch_history(context, "Patch Name", "meta.name");

  if (!name_valid && !patch.name.empty()) {
    ImGui::SameLine();
    ImGui::TextColored(styles::color(styles::MegatoyCol::StatusWarning),
                       "Invalid filename");
  }
  ImGui::PopItemWidth();
}

void render_patch_metadata(PatchEditorContext &context, ym2612::Patch &patch,
                           PatchEditorState &state) {
  const bool name_valid = is_patch_name_valid(patch);

  render_save_export_buttons(context, name_valid, state);

  std::optional<patches::ExportFormatInfo> chosen_format;
  if (ImGui::BeginPopup("Export Options")) {
    auto formats = context.session.export_formats();
    bool any = false;
    for (const auto &fmt : formats) {
      any = true;
      std::string label = fmt.label.empty() ? fmt.extension : fmt.label;
      if (!fmt.extension.empty()) {
        label += " (" + fmt.extension + ")";
      }
      if (ImGui::MenuItem(label.c_str())) {
        chosen_format = fmt;
      }
    }
    if (!any) {
      ImGui::MenuItem("No export formats available", nullptr, false, false);
    }
    ImGui::EndPopup();
  }

  if (chosen_format) {
    trigger_export(context.session, state, *chosen_format);
  }

  render_patch_name_field(context, patch, name_valid);

  ImGui::Spacing();
}

void render_lfo_section(PatchEditorContext &context, ym2612::Patch &patch,
                        bool &settings_changed) {
  ImGui::SeparatorText("Low Frequency Oscillator");
  bool lfo_enable = patch.global.lfo_enable;
  if (ImGui::Checkbox("LFO Enable", &lfo_enable)) {
    patch.global.lfo_enable = lfo_enable;
    settings_changed = true;
  }
  track_patch_history(context, "LFO Enable", "global.lfo_enable");

  ImGui::PushItemWidth(hslider_width);

  int lfo_freq = patch.global.lfo_frequency;

  if (!lfo_enable)
    ImGui::BeginDisabled(true);
  bool lfo_freq_changed = ImGui::SliderInt("LFO Frequency", &lfo_freq, 0, 7);

  track_patch_history(context, "LFO Frequency", "global.lfo_frequency");
  if (lfo_freq_changed) {
    patch.global.lfo_frequency = static_cast<uint8_t>(lfo_freq);
    settings_changed = true;
  }

  ImGui::Spacing();

  int ams = patch.channel.amplitude_modulation_sensitivity;
  bool ams_changed =
      ImGui::SliderInt("Amplitude Modulation Sensitivity", &ams, 0, 3);
  track_patch_history(context, "Amplitude Modulation Sensitivity",
                      "channel.am_sensitivity");
  if (ams_changed) {
    patch.channel.amplitude_modulation_sensitivity = static_cast<uint8_t>(ams);
    settings_changed = true;
  }

  int fms = patch.channel.frequency_modulation_sensitivity;
  bool fms_changed =
      ImGui::SliderInt("Frequency Modulation Sensitivity", &fms, 0, 7);
  track_patch_history(context, "Frequency Modulation Sensitivity",
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

void render_channel_section(PatchEditorContext &context, ym2612::Patch &patch,
                            bool &settings_changed) {
  ImGui::SeparatorText("Channel");
  bool left_speaker = patch.channel.left_speaker;
  if (ImGui::Checkbox("Left Speaker", &left_speaker)) {
    patch.channel.left_speaker = left_speaker;
    settings_changed = true;
  }
  track_patch_history(context, "Left Speaker", "channel.left_speaker");

  ImGui::SameLine();

  bool right_speaker = patch.channel.right_speaker;
  if (ImGui::Checkbox("Right Speaker", &right_speaker)) {
    patch.channel.right_speaker = right_speaker;
    settings_changed = true;
  }
  track_patch_history(context, "Right Speaker", "channel.right_speaker");

  ImGui::PushItemWidth(hslider_width);

  if (const auto *preview =
          get_algorithm_preview_texture(patch.instrument.algorithm)) {
    ImGui::Image(preview->texture_id, preview->size);
  }

  int algorithm = patch.instrument.algorithm;
  bool algorithm_changed = ImGui::SliderInt("Algorithm", &algorithm, 0, 7);
  track_patch_history(context, "Algorithm", "instrument.algorithm");
  if (algorithm_changed) {
    patch.instrument.algorithm = static_cast<uint8_t>(algorithm);
    settings_changed = true;
  }
  ImGui::PopItemWidth();

  ImGui::Spacing();
}

void render_operator_section(PatchEditorContext &context, ym2612::Patch &patch,
                             bool &settings_changed) {

  const auto avail_width = ImGui::GetContentRegionAvail().x;
  bool space_for_feedbacks[4] = {false};
  if (avail_width > 250.0f * 4) {
    ImGui::Columns(4, "operation_columns", false);
    space_for_feedbacks[0] = true;
    space_for_feedbacks[1] = true;
    space_for_feedbacks[2] = true;
    space_for_feedbacks[3] = true;
  } else if (avail_width > 250.0f * 2) {
    ImGui::Columns(2, "operation_columns", false);
    space_for_feedbacks[0] = true;
    space_for_feedbacks[1] = true;
  } else {
    ImGui::Columns(1, "operation_columns", false);
  }
  for (auto i = 0; i < 4; i++) {
    auto op_index = static_cast<int>(ym2612::all_operator_indices[i]);

    settings_changed |= render_operator_editor(
        context, patch, patch.instrument.operators[op_index], i,
        context.envelope_states[i], space_for_feedbacks[i]);

    ImGui::Spacing();
    ImGui::NextColumn();
  }
  ImGui::Columns(1);
}

} // namespace

// Function to render instrument settings panel
void render_patch_editor(const char *title, PatchEditorContext &context,
                         PatchEditorState &state) {
  auto &patch = context.session.current_patch();
  auto is_modified = context.session.is_modified();

  if (!context.prefs.show_patch_editor) {
    return;
  }

  ImGui::SetNextWindowPos(ImVec2(400, 50), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(400, 600), ImGuiCond_FirstUseEver);

  auto title_with_id =
      std::string(title) + (is_modified ? " *###" : "###") + std::string(title);
  if (!ImGui::Begin(title_with_id.c_str(), &context.prefs.show_patch_editor)) {
    ImGui::End();
    return;
  }

  bool settings_changed = false;

  render_patch_metadata(context, patch, state);

  const auto available_width = ImGui::GetContentRegionAvail().x;
  ImGui::Columns(available_width > 800 ? 2 : 1, "##lfo_channel_columns", false);
  render_lfo_section(context, patch, settings_changed);
  ImGui::NextColumn();
  render_channel_section(context, patch, settings_changed);
  ImGui::Columns(1);
  render_operator_section(context, patch, settings_changed);

  // Apply settings if changed
  if (settings_changed) {
    context.session.apply_patch_to_audio();
  }

  ImGui::End();
}

} // namespace ui
