#include "operator_editor.hpp"
#include "history_helpers.hpp"
#include "preview/ssg_preview.hpp"
#include <imgui.h>
#include <string>

namespace ui {

// Helper function to render operator settings
void render_operator_editor(AppState &app_state, ym2612::OperatorSettings &op,
                            int op_num) {
  std::string op_label = "Operator " + std::to_string(op_num);
  std::string key_prefix = "instrument.op" + std::to_string(op_num);
  ImGui::SeparatorText(op_label.c_str());
  ImGui::PushID(op_num);
  ImGui::PushItemWidth(150);

  // Amplitude Modulation Enable
  bool amplitude_mod = op.amplitude_modulation_enable;
  if (ImGui::Checkbox("Amplitude Modulation Enable", &amplitude_mod)) {
    op.amplitude_modulation_enable = amplitude_mod;
  }
  track_patch_history(app_state, op_label + " Amplitude Modulation",
                      key_prefix + ".am_enable");

  // Total Level (0-127)
  int total_level = op.total_level;
  bool total_changed = ImGui::SliderInt("Total Level", &total_level, 127, 0);
  track_patch_history(app_state, op_label + " Total Level",
                      key_prefix + ".total_level");
  if (total_changed) {
    op.total_level = static_cast<uint8_t>(total_level);
  }

  ImGui::Spacing();

  // Attack Rate (0-31)
  int attack_rate = op.attack_rate;
  bool attack_changed = ImGui::SliderInt("Attack Rate", &attack_rate, 0, 31);
  track_patch_history(app_state, op_label + " Attack Rate",
                      key_prefix + ".attack_rate");
  if (attack_changed) {
    op.attack_rate = static_cast<uint8_t>(attack_rate);
  }

  // Decay Rate (0-31)
  int decay_rate = op.decay_rate;
  bool decay_changed = ImGui::SliderInt("Decay Rate", &decay_rate, 31, 0);
  track_patch_history(app_state, op_label + " Decay Rate",
                      key_prefix + ".decay_rate");
  if (decay_changed) {
    op.decay_rate = static_cast<uint8_t>(decay_rate);
  }

  // Sustain Level (0-15)
  int sustain_level = op.sustain_level;
  bool sustain_level_changed =
      ImGui::SliderInt("Sustain Level", &sustain_level, 15, 0);
  track_patch_history(app_state, op_label + " Sustain Level",
                      key_prefix + ".sustain_level");
  if (sustain_level_changed) {
    op.sustain_level = static_cast<uint8_t>(sustain_level);
  }

  // Sustain Rate (0-31)
  int sustain_rate = op.sustain_rate;
  bool sustain_rate_changed =
      ImGui::SliderInt("Sustain Rate", &sustain_rate, 31, 0);
  track_patch_history(app_state, op_label + " Sustain Rate",
                      key_prefix + ".sustain_rate");
  if (sustain_rate_changed) {
    op.sustain_rate = static_cast<uint8_t>(sustain_rate);
  }

  // Release Rate (0-15)
  int release_rate = op.release_rate;
  bool release_changed = ImGui::SliderInt("Release Rate", &release_rate, 15, 0);
  track_patch_history(app_state, op_label + " Release Rate",
                      key_prefix + ".release_rate");
  if (release_changed) {
    op.release_rate = static_cast<uint8_t>(release_rate);
  }

  ImGui::Spacing();

  // Key Scale (0-3)
  int key_scale = op.key_scale;
  bool key_scale_changed = ImGui::SliderInt("Key Scale", &key_scale, 0, 3);
  track_patch_history(app_state, op_label + " Key Scale",
                      key_prefix + ".key_scale");
  if (key_scale_changed) {
    op.key_scale = static_cast<uint8_t>(key_scale);
  }

  // Multiple (0-15)
  int multiple = op.multiple;
  bool multiple_changed = ImGui::SliderInt("Multiple", &multiple, 0, 15);
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
  ImGui::PopItemWidth();
  ImGui::PopID();
}

} // namespace ui
