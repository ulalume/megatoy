#pragma once

#include <imgui.h>

namespace ui {

/// A frame `thickness` wide just inside `min`..`max`, as four strips: a
/// stroked rectangle is antialiased and bleeds a pixel either side of the
/// edge.
inline void draw_sharp_frame(ImDrawList *draw_list, const ImVec2 &min,
                             const ImVec2 &max, ImU32 color, float thickness) {
  const float t = thickness;
  draw_list->AddRectFilled(min, ImVec2(max.x, min.y + t), color);
  draw_list->AddRectFilled(ImVec2(min.x, max.y - t), max, color);
  draw_list->AddRectFilled(ImVec2(min.x, min.y + t),
                           ImVec2(min.x + t, max.y - t), color);
  draw_list->AddRectFilled(ImVec2(max.x - t, min.y + t),
                           ImVec2(max.x, max.y - t), color);
}

} // namespace ui
