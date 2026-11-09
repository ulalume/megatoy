#include "ssg_preview.hpp"

#include "gui/components/common.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <imgui.h>

namespace ui {
namespace {

/// A point of an envelope's outline: x in cycles from 0 to 4, level from 0
/// at the floor to 1 at the top.
struct Point {
  float cycle;
  float level;
};

struct Shape {
  std::array<Point, 8> points;
  int count;
};

/// Each type's outline. The attack is instant, so a decaying type starts at
/// the top and an inverted one at the floor.
constexpr std::array<Shape, 8> kShapes = {{
    {{{{0, 1}, {1, 0}, {1, 1}, {2, 0}, {2, 1}, {3, 0}, {3, 1}, {4, 0}}}, 8},
    {{{{0, 1}, {1, 0}, {4, 0}}}, 3},
    {{{{0, 1}, {1, 0}, {2, 1}, {3, 0}, {4, 1}}}, 5},
    {{{{0, 1}, {1, 0}, {1, 1}, {4, 1}}}, 4},
    {{{{0, 0}, {1, 1}, {1, 0}, {2, 1}, {2, 0}, {3, 1}, {3, 0}, {4, 1}}}, 8},
    {{{{0, 0}, {1, 1}, {4, 1}}}, 3},
    {{{{0, 0}, {1, 1}, {2, 0}, {3, 1}, {4, 0}}}, 5},
    {{{{0, 0}, {1, 1}, {1, 0}, {4, 0}}}, 4},
}};

/// The frame, as four strips so its edges stay sharp.
void draw_frame(ImDrawList *draw_list, ImVec2 min, ImVec2 max, ImU32 color) {
  const float width = ui::scale::px(1.0f);
  draw_list->AddRectFilled(min, ImVec2(max.x, min.y + width), color);
  draw_list->AddRectFilled(ImVec2(min.x, max.y - width), max, color);
  draw_list->AddRectFilled(ImVec2(min.x, min.y + width),
                           ImVec2(min.x + width, max.y - width), color);
  draw_list->AddRectFilled(ImVec2(max.x - width, min.y + width),
                           ImVec2(max.x, max.y - width), color);
}

} // namespace

void ssg_diagram(bool enabled, int envelope_type, ImVec2 size) {
  ImGui::Dummy(size);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  draw_frame(draw_list, min, max,
             styles::color_u32(styles::MegatoyCol::PreviewBorder));

  if (!enabled) {
    const char *label = "OFF";
    const ImVec2 text = ImGui::CalcTextSize(label);
    draw_list->AddText(ImVec2(std::round((min.x + max.x - text.x) * 0.5f),
                              std::round((min.y + max.y - text.y) * 0.5f)),
                       styles::color_u32(styles::MegatoyCol::TextMuted), label);
    return;
  }

  // The cycles fill the frame but for a margin as wide as the space above
  // and below them.
  const float margin = std::round((max.y - min.y) * 0.25f);
  const float left = min.x + margin;
  const float right = max.x - margin;
  const float top = min.y + margin;
  const float bottom = max.y - margin;
  const Shape &shape =
      kShapes[static_cast<std::size_t>(std::clamp(envelope_type, 0, 7))];
  const auto at = [&](const Point &point) {
    return ImVec2(std::round(left + (right - left) * point.cycle / 4.0f),
                  std::round(bottom - (bottom - top) * point.level));
  };

  const ImU32 line = styles::color_u32(styles::MegatoyCol::EnvelopeCurve);
  const ImU32 fill = color_with_alpha(line, 0.4f);
  // Without anti-aliasing, so neighbouring pieces of the fill meet without a
  // seam and the outline sits on whole pixels.
  const ImDrawListFlags flags = draw_list->Flags;
  draw_list->Flags &=
      ~(ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines);
  // The fill under each slope, down to the floor.
  for (int i = 1; i < shape.count; ++i) {
    const ImVec2 from = at(shape.points[i - 1]);
    const ImVec2 to = at(shape.points[i]);
    if (to.x <= from.x) {
      continue;
    }
    const ImVec2 quad[] = {from, to, ImVec2(to.x, bottom),
                           ImVec2(from.x, bottom)};
    draw_list->AddConvexPolyFilled(quad, 4, fill);
  }
  // The outline on the pixel centres, so it stays sharp.
  for (int i = 0; i < shape.count; ++i) {
    const ImVec2 point = at(shape.points[i]);
    draw_list->PathLineTo(ImVec2(point.x + 0.5f, point.y + 0.5f));
  }
  draw_list->PathStroke(line, ImDrawFlags_None, ui::scale::px(1.0f));
  draw_list->Flags = flags;
}

} // namespace ui
