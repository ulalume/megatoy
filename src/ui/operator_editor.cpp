#include "operator_editor.hpp"
#include "../formats/common.hpp"
#include "app_state.hpp"
#include "envelope_image.hpp"
#include "history_helpers.hpp"
#include "preview/ssg_preview.hpp"
#include "ym2612/types.hpp"
#include <imgui.h>
#include <string>

namespace ui {
void text_centered(std::string text, float frame_width) {
  ImGui::Dummy(ImVec2(frame_width, ImGui::GetTextLineHeight()));

  ImVec2 text_size = ImGui::CalcTextSize(text.c_str());
  ImVec2 cursor_pos = ImGui::GetItemRectMin();
  cursor_pos.x += (frame_width - text_size.x) * 0.5f;

  ImGui::GetWindowDrawList()->AddText(
      cursor_pos, ImGui::GetColorU32(ImGuiCol_Text), text.c_str());
}

// Helper function to update slider state
inline void
update_slider_state(UIState::EnvelopeState::SliderState &slider_state) {
  if (ImGui::IsItemActive()) {
    slider_state = UIState::EnvelopeState::SliderState::Active;
  } else if (ImGui::IsItemHovered()) {
    slider_state = UIState::EnvelopeState::SliderState::Hover;
  } else {
    slider_state = UIState::EnvelopeState::SliderState::None;
  }
}

void render_envelope(AppState &app_state, ym2612::OperatorSettings &op,
                     UIState::EnvelopeState &envelope_state,
                     std::string op_label, std::string key_prefix) {

  ImGui::BeginGroup(); // ADSR Envelope group
  render_envelope_image(op, envelope_state, image_size);

  ImGui::BeginGroup();
  text_centered("TL", vslider_width);
  ImGui::SameLine();
  text_centered("AR", vslider_width);
  ImGui::SameLine();
  text_centered("DR", vslider_width);
  ImGui::SameLine();
  text_centered("SL", vslider_width);
  ImGui::SameLine();
  text_centered("SR", vslider_width);
  ImGui::SameLine();
  text_centered("RR", vslider_width);
  ImGui::EndGroup();

  // Attack Rate (0-31)
  ImGui::BeginGroup();

  // Total Level (0-127)
  int total_level = op.total_level;
  bool total_changed =
      ImGui::VSliderInt("##Total Level", vslider_size, &total_level, 127, 0);
  update_slider_state(envelope_state.total_level);
  track_patch_history(app_state, op_label + " Total Level",
                      key_prefix + ".total_level");
  if (total_changed) {
    op.total_level = static_cast<uint8_t>(total_level);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Total Level");
  }

  ImGui::SameLine();

  int attack_rate = op.attack_rate;
  bool attack_changed =
      ImGui::VSliderInt("##Attack Rate", vslider_size, &attack_rate, 31, 0);
  update_slider_state(envelope_state.attack_rate);
  track_patch_history(app_state, op_label + " Attack Rate",
                      key_prefix + ".attack_rate");
  if (attack_changed) {
    op.attack_rate = static_cast<uint8_t>(attack_rate);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Attack Rate");
  }
  ImGui::SameLine();

  // Decay Rate (0-31)
  int decay_rate = op.decay_rate;
  bool decay_changed =
      ImGui::VSliderInt("##Decay Rate", vslider_size, &decay_rate, 31, 0);
  update_slider_state(envelope_state.decay_rate);
  track_patch_history(app_state, op_label + " Decay Rate",
                      key_prefix + ".decay_rate");
  if (decay_changed) {
    op.decay_rate = static_cast<uint8_t>(decay_rate);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Decay Rate");
  }
  ImGui::SameLine();

  // Sustain Level (0-15)
  int sustain_level = op.sustain_level;
  bool sustain_level_changed =
      ImGui::VSliderInt("##Sustain Level", vslider_size, &sustain_level, 15, 0);
  update_slider_state(envelope_state.sustain_level);
  track_patch_history(app_state, op_label + " Sustain Level",
                      key_prefix + ".sustain_level");
  if (sustain_level_changed) {
    op.sustain_level = static_cast<uint8_t>(sustain_level);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Sustain Level");
  };
  ImGui::SameLine();

  // Sustain Rate (0-31)
  int sustain_rate = op.sustain_rate;
  bool sustain_rate_changed =
      ImGui::VSliderInt("##Sustain Rate", vslider_size, &sustain_rate, 31, 0);
  update_slider_state(envelope_state.sustain_rate);
  track_patch_history(app_state, op_label + " Sustain Rate",
                      key_prefix + ".sustain_rate");
  if (sustain_rate_changed) {
    op.sustain_rate = static_cast<uint8_t>(sustain_rate);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Sustain Rate");
  }
  ImGui::SameLine();

  // Release Rate (0-15)
  int release_rate = op.release_rate;
  bool release_changed =
      ImGui::VSliderInt("##Release Rate", vslider_size, &release_rate, 15, 0);
  update_slider_state(envelope_state.release_rate);
  track_patch_history(app_state, op_label + " Release Rate",
                      key_prefix + ".release_rate");
  if (release_changed) {
    op.release_rate = static_cast<uint8_t>(release_rate);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Release Rate");
  }
  ImGui::EndGroup();

  ImGui::EndGroup(); // End ADSR Envelope group
}

// Helper function to render operator settings
void render_operator_editor(AppState &app_state, ym2612::OperatorSettings &op,
                            int op_index) {

  const auto column_layout = ImGui::GetContentRegionAvail().x > 410.0f;

  ym2612::OperatorIndex op_enum = ym2612::all_operator_indices[op_index];
  auto is_modulator =
      static_cast<int>(op_enum) <
      ym2612::algorithm_modulator_count[app_state.patch().instrument.algorithm];
  std::string op_label = "Operator " + std::to_string(op_index + 1) +
                         (is_modulator ? "" : " (Carrier)");
  std::string key_prefix = "instrument.op" + std::to_string(op_index);

  if (!is_modulator) {
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
    ImGui::PushStyleColor(ImGuiCol_Separator,
                          ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
  }
  ImGui::SeparatorText(op_label.c_str());
  if (!is_modulator) {
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
  }

  ImGui::PushID(op_index);
  ImGui::PushItemWidth(hslider_width);

  // Amplitude Modulation Enable
  bool amplitude_mod = op.amplitude_modulation_enable;
  if (ImGui::Checkbox("Amplitude Modulation Enable", &amplitude_mod)) {
    op.amplitude_modulation_enable = amplitude_mod;
  }
  ImGui::Spacing();

  track_patch_history(app_state, op_label + " Amplitude Modulation",
                      key_prefix + ".am_enable");

  render_envelope(app_state, op, app_state.ui_state().envelope_states[op_index],
                  op_label, key_prefix);

  if (column_layout) {
    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();
    ImGui::BeginGroup();
  } else {
    ImGui::Spacing();
  }

  // SSG Enable
  //
  // SSG Type Envelope Control (0-7)
  int ssg_type = op.ssg_type_envelope_control;
  if (const auto *preview = op.ssg_enable ? get_ssg_preview_texture(ssg_type)
                                          : get_ssg_preview_off_texture()) {
    if (preview->valid()) {
      ImGui::Image(preview->texture_id, preview->size);
    }
  }
  ImGui::SameLine();

  bool ssg_enable = op.ssg_enable;
  if (ImGui::Checkbox("SSG EG Enable", &ssg_enable)) {
    op.ssg_enable = ssg_enable;
  }
  track_patch_history(app_state, op_label + " SSG EG Enable",
                      key_prefix + ".ssg_enable");

  bool ssg_type_changed = ImGui::SliderInt("SSG EG Type", &ssg_type, 0, 7);
  track_patch_history(app_state, op_label + " SSG EG Type",
                      key_prefix + ".ssg_type");
  if (ssg_type_changed) {
    op.ssg_type_envelope_control = static_cast<uint8_t>(ssg_type);
  }

  if (column_layout) {
    ImVec2 pos = ImGui::GetCursorPos();
    ImGui::SetCursorPosY(pos.y + 123);
  } else {
    ImGui::Spacing();
  }

  // Key Scale (0-3)
  int key_scale = op.key_scale;
  bool key_scale_changed = ImGui::SliderInt("Key Scale", &key_scale, 0, 3);
  track_patch_history(app_state, op_label + " Key Scale",
                      key_prefix + ".key_scale");
  if (key_scale_changed) {
    op.key_scale = static_cast<uint8_t>(key_scale);
  }

  // Multiple (0-15)
  const char *multiple_labels[] = {"0.5", "1",  "2",  "3", "4",  "5",
                                   "6",   "7",  "8",  "9", "10", "11",
                                   "12",  "13", "14", "15"};
  int multiple = op.multiple;
  bool multiple_changed =
      ImGui::SliderInt("Multiple", &multiple, 0, 15, multiple_labels[multiple]);
  track_patch_history(app_state, op_label + " Multiple",
                      key_prefix + ".multiple");
  if (multiple_changed) {
    op.multiple = static_cast<uint8_t>(multiple);
  }

  // Detune (0-7)
  static const char *labels[] = {
      "0", "+1", "+2", "+3", "0", "-1", "-2", "-3",
  };
  int detune = op.detune;
  bool detune_changed =
      ImGui::SliderInt("Detune", &detune, 0, 7, labels[detune]);
  track_patch_history(app_state, op_label + " Detune", key_prefix + ".detune");
  if (detune_changed) {
    op.detune = static_cast<uint8_t>(detune);
  }

  if (column_layout) {
    ImGui::EndGroup();
  }
  ImGui::PopItemWidth();

  ImGui::PopID();
}

} // namespace ui
