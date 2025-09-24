#include "operator_editor.hpp"
#include "preview/ssg_preview.hpp"
#include <cstring>
#include <imgui.h>
#include <string>

namespace ui {

// Helper function to render operator settings
void render_operator_editor(ym2612::OperatorSettings &op, int op_num) {
  std::string op_label = "Operator " + std::to_string(op_num);
  ImGui::SeparatorText(op_label.c_str());
  ImGui::PushID(op_num);
  ImGui::PushItemWidth(150);

  // Amplitude Modulation Enable
  ImGui::Checkbox("Amplitude Modulation Enable",
                  &op.amplitude_modulation_enable);

  // Total Level (0-127)
  int total_level = op.total_level;
  if (ImGui::SliderInt("Total Level", &total_level, 127, 0)) {
    op.total_level = static_cast<uint8_t>(total_level);
  }

  ImGui::Spacing();

  // Attack Rate (0-31)
  int attack_rate = op.attack_rate;
  if (ImGui::SliderInt("Attack Rate", &attack_rate, 0, 31)) {
    op.attack_rate = static_cast<uint8_t>(attack_rate);
  }

  // Decay Rate (0-31)
  int decay_rate = op.decay_rate;
  if (ImGui::SliderInt("Decay Rate", &decay_rate, 0, 31)) {
    op.decay_rate = static_cast<uint8_t>(decay_rate);
  }

  // Sustain Level (0-15)
  int sustain_level = op.sustain_level;
  if (ImGui::SliderInt("Sustain Level", &sustain_level, 15, 0)) {
    op.sustain_level = static_cast<uint8_t>(sustain_level);
  }

  // Sustain Rate (0-31)
  int sustain_rate = op.sustain_rate;
  if (ImGui::SliderInt("Sustain Rate", &sustain_rate, 0, 31)) {
    op.sustain_rate = static_cast<uint8_t>(sustain_rate);
  }

  // Release Rate (0-15)
  int release_rate = op.release_rate;
  if (ImGui::SliderInt("Release Rate", &release_rate, 0, 15)) {
    op.release_rate = static_cast<uint8_t>(release_rate);
  }

  ImGui::Spacing();

  // Key Scale (0-3)
  int key_scale = op.key_scale;
  if (ImGui::SliderInt("Key Scale", &key_scale, 0, 3)) {
    op.key_scale = static_cast<uint8_t>(key_scale);
  }

  // Multiple (0-15)
  int multiple = op.multiple;
  if (ImGui::SliderInt("Multiple", &multiple, 0, 15)) {
    op.multiple = static_cast<uint8_t>(multiple);
  }

  // Detune (0-7)
  int detune = op.detune;
  static const char *labels[] = {
      "0", "+1", "+2", "+3", "0", "-1", "-2", "-3",
  };
  if (ImGui::SliderInt("Detune", &detune, 0, 7, labels[detune])) {
    op.detune = static_cast<uint8_t>(detune);
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

  ImGui::Checkbox("SSG Enable", &op.ssg_enable);

  if (ImGui::SliderInt("SSG Type", &ssg_type, 0, 7)) {
    op.ssg_type_envelope_control = static_cast<uint8_t>(ssg_type);
  }
  ImGui::PopItemWidth();
  ImGui::PopID();
}

} // namespace ui
