#pragma once

#include <imgui.h>

namespace ui {

/// A framed area to draw a trace in: filled background, clipped contents,
/// border drawn last so it stays crisp over whatever was plotted.
struct Panel {
  ImDrawList *draw_list;
  ImVec2 min;
  ImVec2 max;
};

/// Claims `size` of layout as an item, so the panel can be clicked and the
/// caller keeps ImGui's cursor.
inline Panel begin_panel(const ImVec2 &size, const char *id) {
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton(id, size);

  Panel panel{ImGui::GetWindowDrawList(), origin,
              ImVec2(origin.x + size.x, origin.y + size.y)};
  panel.draw_list->AddRectFilled(panel.min, panel.max,
                                 ImGui::GetColorU32(ImGuiCol_FrameBg));
  panel.draw_list->PushClipRect(panel.min, panel.max, true);
  return panel;
}

inline void end_panel(const Panel &panel) {
  panel.draw_list->PopClipRect();
  panel.draw_list->AddRect(panel.min, panel.max,
                           ImGui::GetColorU32(ImGuiCol_Border));
}

} // namespace ui
