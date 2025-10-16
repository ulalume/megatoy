#include "envelope_image.hpp"
#include "app_state.hpp"
#include "common.hpp"
#include <imgui.h>

namespace ui {
ImVec2 operator+(const ImVec2 &lhs, const ImVec2 &rhs) {
  return ImVec2(lhs.x + rhs.x, lhs.y + rhs.y);
}

ImVec2 operator-(const ImVec2 &lhs, const ImVec2 &rhs) {
  return ImVec2(lhs.x - rhs.x, lhs.y - rhs.y);
}

int compute_effective_rate_attack(int attack_rate, int rate_key_scaling = 0) {
  return 2 * attack_rate + rate_key_scaling; // 0 ~ 62
}
int compute_effective_rate_decay(int decay_rate, int rate_key_scaling = 0) {
  return decay_rate + rate_key_scaling; // 0 ~ 31
}
int compute_effective_rate_release(int release_rate, int rate_key_scaling = 0) {
  return 2 * release_rate + 1 + rate_key_scaling; // 0 ~ 31
}
ImU32 color_from_slider_state(
    const UIState::EnvelopeState::SliderState &state) {
  switch (state) {
  case UIState::EnvelopeState::SliderState::None:
    return ImGui::GetColorU32(ImGuiCol_Text);
  case UIState::EnvelopeState::SliderState::Hover:
    return ImGui::GetColorU32(ImGuiCol_FrameBgActive);
  case UIState::EnvelopeState::SliderState::Active:
    return ImGui::GetColorU32(ImGuiCol_FrameBgActive);
  }
}

void render_envelope_image(const ym2612::OperatorSettings &op,
                           const UIState::EnvelopeState &state, ImVec2 size) {
  ImGui::BeginChild("EnvelopeImage", size, false, ImGuiWindowFlags_NoScrollbar);

  ImDrawList *draw_list = ImGui::GetWindowDrawList();

  ImVec2 canvas_min = ImGui::GetCursorScreenPos();
  ImVec2 canvas_max = ImVec2(canvas_min.x + size.x, canvas_min.y + size.y);

  // draw border
  ImU32 border_color = ImGui::GetColorU32(ImGuiCol_Separator);
  draw_list->AddRect(canvas_min, canvas_max, border_color);

  const float draw_width = size.x - 2;
  const float draw_height = size.y - 2;
  const ImVec2 draw_min = ImVec2(canvas_min.x, canvas_min.y);

  auto total_level_height = draw_height * op.total_level / 127.0f;
  auto sustain_level_height =
      total_level_height +
      (draw_height - total_level_height) * op.sustain_level / 15.0f;
  const float line_thickness = 3.0f;

  ImVec2 envelope_points[5];
  ImU32 envelope_colors[4];
  envelope_points[0] = ImVec2(0, draw_height);
  envelope_colors[0] = color_from_slider_state(state.attack_rate);
  envelope_colors[1] = color_from_slider_state(state.decay_rate);
  envelope_colors[2] = color_from_slider_state(state.sustain_rate);
  envelope_colors[3] =
      color_from_slider_state(UIState::EnvelopeState::SliderState::None);

  // Attack Rate
  if (op.attack_rate == 0) {
    envelope_points[1] = envelope_points[0];
    envelope_points[2] = envelope_points[0];
    envelope_points[3] = envelope_points[0];
    envelope_colors[1] = color_from_slider_state(state.attack_rate);
    envelope_colors[2] = color_from_slider_state(state.attack_rate);
    envelope_colors[3] = color_from_slider_state(state.attack_rate);
  } else {
    float attack_width = 0;
    if (op.attack_rate != 31) {
      attack_width += (127.0f - total_level_height) /
                      compute_effective_rate_attack(op.attack_rate);
    }
    envelope_points[1] = ImVec2(attack_width, total_level_height);

    // Decay Rate
    if (op.decay_rate == 0 && op.sustain_level != 0) {
      envelope_points[2] = envelope_points[1];
      envelope_points[3] = envelope_points[1];
      envelope_colors[2] = color_from_slider_state(state.decay_rate);
      envelope_colors[3] = color_from_slider_state(state.decay_rate);
    } else {
      float decay_width = attack_width;
      if (op.decay_rate != 31 && op.sustain_level != 0) {
        decay_width += (sustain_level_height - total_level_height) /
                       compute_effective_rate_decay(op.decay_rate);
      }
      envelope_points[2] = ImVec2(decay_width, sustain_level_height);

      // Sustain Rate
      if (op.sustain_rate == 0) {
        envelope_points[3] = envelope_points[2] + ImVec2(3, 0);
        envelope_colors[3] = color_from_slider_state(state.sustain_rate);
      } else {
        float sustain_rate_width = decay_width;
        if (op.sustain_rate != 31) {
          sustain_rate_width += (draw_height - sustain_level_height) /
                                compute_effective_rate_decay(op.sustain_rate);
        }
        envelope_points[3] = ImVec2(sustain_rate_width, draw_height);
      }
    }
  }

  float release_width = (draw_height - total_level_height) /
                        compute_effective_rate_release(op.release_rate);
  auto r = fmin(4.0f, draw_width / fmax(release_width, envelope_points[3].x));
  for (int i = 0; i < 4; i++) {
    envelope_points[i].x *= r;
  }
  release_width *= r;
  ImVec2 rr0 = ImVec2(0, total_level_height);
  ImVec2 rr1 = ImVec2(release_width, draw_height);
  ImVec2 rr2 = ImVec2(0, draw_height);

  // draw phase

  // draw grid
  ImU32 grid_color = ImGui::GetColorU32(ImGuiCol_Separator);

  auto i = 0;
  while (true) {
    auto grid_x = i * 12.4f * r;
    if (grid_x > draw_width)
      break;
    draw_list->AddLine(ImVec2(grid_x, 0) + draw_min,
                       ImVec2(grid_x, draw_height) + draw_min, grid_color);
    i++;
  }
  i = 0;
  while (true) {
    auto grid_y = i * 12.4f * 4.0f;
    if (grid_y > draw_height)
      break;
    draw_list->AddLine(ImVec2(0, draw_height - grid_y) + draw_min,
                       ImVec2(draw_width, draw_height - grid_y) + draw_min,
                       grid_color);
    i++;
  }

  // draw Release Rate
  draw_list->AddTriangleFilled(
      rr0 + draw_min, rr1 + draw_min, rr2 + draw_min,
      color_with_alpha(color_from_slider_state(state.release_rate), 0.4f));

  // draw Envelope
  envelope_points[4] = ImVec2(draw_width, envelope_points[3].y);
  for (int i = 0; i < 4; i++) {
    draw_list->AddLine(envelope_points[i] + draw_min,
                       envelope_points[i + 1] + draw_min, envelope_colors[i],
                       line_thickness);
  }
  // draw Total Level
  if (state.total_level != UIState::EnvelopeState::SliderState::None) {
    draw_list->AddLine(ImVec2(0, total_level_height) + draw_min,
                       ImVec2(draw_width, total_level_height) + draw_min,
                       color_from_slider_state(state.total_level), 1.0f);
  }
  // draw Sustain Level
  if (state.sustain_level != UIState::EnvelopeState::SliderState::None) {
    draw_list->AddLine(ImVec2(0, sustain_level_height) + draw_min,
                       ImVec2(draw_width, sustain_level_height) + draw_min,
                       color_from_slider_state(state.sustain_level), 1.0f);
  }

  ImGui::EndChild();
}

} // namespace ui
