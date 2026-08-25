#include "envelope_image.hpp"
#include "app_state.hpp"
#include "common.hpp"
#include "gui/ui_scale.hpp"
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
  // Default case to prevent warning
  return ImGui::GetColorU32(ImGuiCol_FrameBg);
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

  // The curve is built in register units -- levels are attenuation from 0
  // (loudest) to kMaxLevel (silent), and a time is a level span divided by a
  // rate -- so its shape depends only on the patch. Pixels arrive at the end.
  constexpr float kMaxLevel = 127.0f;
  constexpr float kTimeGridStep = kMaxLevel / 8.0f;
  constexpr float kLevelGridStep = kMaxLevel / 2.0f;
  // Below this span the envelope stops stretching to fill the width, so a
  // fast envelope still reads as fast.
  constexpr float kMinSpan = 50.0f;
  // Sustain rate 0 holds forever; a stub keeps the segment visible.
  constexpr float kHeldSustainTime = 4.0f;

  const float total_level = static_cast<float>(op.total_level);
  const float sustain_level =
      total_level + (kMaxLevel - total_level) * op.sustain_level / 15.0f;
  const float line_thickness = ui::scale::px(3.0f);

  auto y_for = [draw_height](float level) {
    return draw_height * level / kMaxLevel;
  };

  // x is a time, y is a level, until the conversion below.
  ImVec2 envelope_points[5];
  ImU32 envelope_colors[4];
  envelope_points[0] = ImVec2(0.0f, kMaxLevel);
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
    float attack_time = 0.0f;
    if (op.attack_rate != 31) {
      attack_time = (kMaxLevel - total_level) /
                    compute_effective_rate_attack(op.attack_rate);
    }
    envelope_points[1] = ImVec2(attack_time, total_level);

    // Decay Rate
    if (op.decay_rate == 0 && op.sustain_level != 0) {
      envelope_points[2] = envelope_points[1];
      envelope_points[3] = envelope_points[1];
      envelope_colors[2] = color_from_slider_state(state.decay_rate);
      envelope_colors[3] = color_from_slider_state(state.decay_rate);
    } else {
      float decay_time = attack_time;
      if (op.decay_rate != 31 && op.sustain_level != 0) {
        decay_time += (sustain_level - total_level) /
                      compute_effective_rate_decay(op.decay_rate);
      }
      envelope_points[2] = ImVec2(decay_time, sustain_level);

      // Sustain Rate
      if (op.sustain_rate == 0) {
        envelope_points[3] =
            envelope_points[2] + ImVec2(kHeldSustainTime, 0.0f);
        envelope_colors[3] = color_from_slider_state(state.sustain_rate);
      } else {
        float sustain_time = decay_time;
        if (op.sustain_rate != 31) {
          sustain_time += (kMaxLevel - sustain_level) /
                          compute_effective_rate_decay(op.sustain_rate);
        }
        envelope_points[3] = ImVec2(sustain_time, kMaxLevel);
      }
    }
  }

  const float release_time = (kMaxLevel - total_level) /
                             compute_effective_rate_release(op.release_rate);

  // Pixels per time unit, chosen so the longer of the two curves fits.
  const float span = fmax(fmax(release_time, envelope_points[3].x), kMinSpan);
  const float time_scale = draw_width / span;

  for (auto &point : envelope_points) {
    point = ImVec2(point.x * time_scale, y_for(point.y));
  }

  const ImVec2 rr0 = ImVec2(0.0f, y_for(total_level));
  const ImVec2 rr1 = ImVec2(release_time * time_scale, draw_height);
  const ImVec2 rr2 = ImVec2(0.0f, draw_height);

  // draw phase

  // draw grid
  ImU32 grid_color = ImGui::GetColorU32(ImGuiCol_Separator);

  for (float time = 0.0f; time * time_scale <= draw_width;
       time += kTimeGridStep) {
    const float grid_x = time * time_scale;
    draw_list->AddLine(ImVec2(grid_x, 0.0f) + draw_min,
                       ImVec2(grid_x, draw_height) + draw_min, grid_color);
  }
  for (float level = kMaxLevel; level >= 0.0f; level -= kLevelGridStep) {
    const float grid_y = y_for(level);
    draw_list->AddLine(ImVec2(0.0f, grid_y) + draw_min,
                       ImVec2(draw_width, grid_y) + draw_min, grid_color);
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
  const float marker_thickness = ui::scale::px(1.0f);
  // draw Total Level
  if (state.total_level != UIState::EnvelopeState::SliderState::None) {
    const float y = y_for(total_level);
    draw_list->AddLine(
        ImVec2(0.0f, y) + draw_min, ImVec2(draw_width, y) + draw_min,
        color_from_slider_state(state.total_level), marker_thickness);
  }
  // draw Sustain Level
  if (state.sustain_level != UIState::EnvelopeState::SliderState::None) {
    const float y = y_for(sustain_level);
    draw_list->AddLine(
        ImVec2(0.0f, y) + draw_min, ImVec2(draw_width, y) + draw_min,
        color_from_slider_state(state.sustain_level), marker_thickness);
  }

  ImGui::EndChild();
}

} // namespace ui
