#include "gui/tab_bar_lines.hpp"

#include "gui/styles/megatoy_style.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <vector>

namespace ui {
namespace {

std::vector<ImGuiID> &lit_nodes() {
  static std::vector<ImGuiID> nodes;
  return nodes;
}

bool is_lit(ImGuiID node) {
  for (const ImGuiID lit : lit_nodes()) {
    if (lit == node) {
      return true;
    }
  }
  return false;
}

/// The line along the top of `bar`'s tab under the pointer, drawn where
/// ImGui draws the shown tab's.
void draw_hovered_overline(const ImGuiTabBar &bar, ImDrawList *draw_list) {
  const ImGuiContext &g = *GImGui;
  const ImU32 color = styles::color_u32(styles::MegatoyCol::TabHoveredOverline);
  if ((color & IM_COL32_A_MASK) == 0 || g.Style.TabBarOverlineSize <= 0.0f) {
    return;
  }
  for (const ImGuiTabItem &tab : bar.Tabs) {
    if (tab.ID == 0 || (g.HoveredId != tab.ID && g.ActiveId != tab.ID)) {
      continue;
    }
    const float left = bar.BarRect.Min.x + tab.Offset - bar.ScrollingAnim;
    const float top = bar.BarRect.Min.y + 1.0f * g.CurrentDpiScale;
    const ImVec2 tl(left, top);
    const ImVec2 tr(left + tab.Width, top);
    draw_list->PushClipRect(bar.BarRect.Min, bar.BarRect.Max, true);
    if (g.Style.TabRounding > 0.0f) {
      const float rounding = g.Style.TabRounding;
      draw_list->PathArcToFast(ImVec2(tl.x + rounding, tl.y + rounding),
                               rounding, 7, 9);
      draw_list->PathArcToFast(ImVec2(tr.x - rounding, tr.y + rounding),
                               rounding, 9, 11);
      draw_list->PathStroke(color, 0, g.Style.TabBarOverlineSize);
    } else {
      draw_list->AddLine(ImVec2(tl.x - 0.5f, tl.y - 0.5f),
                         ImVec2(tr.x - 0.5f, tr.y - 0.5f), color,
                         g.Style.TabBarOverlineSize);
    }
    draw_list->PopClipRect();
  }
}

} // namespace

void keep_tab_bar_line_lit() {
  if (const ImGuiDockNode *node = ImGui::GetCurrentWindow()->DockNode) {
    lit_nodes().push_back(node->ID);
  }
}

void draw_tab_bar_lines() {
  const ImGuiContext &g = *GImGui;
  const float thickness = g.Style.TabBarBorderSize;
  if (thickness > 0.0f) {
    for (int i = 0; i < g.DockContext.Nodes.Data.Size; ++i) {
      const auto *node =
          static_cast<ImGuiDockNode *>(g.DockContext.Nodes.Data[i].val_p);
      if (node == nullptr || node->TabBar == nullptr ||
          node->HostWindow == nullptr || !node->IsVisible ||
          node->IsHiddenTabBar() || node->IsNoTabBar() ||
          node->LastFrameActive != g.FrameCount) {
        continue;
      }
      const ImGuiTabBar &bar = *node->TabBar;
      const bool focused =
          (bar.Flags & ImGuiTabBarFlags_IsFocused) != 0 || is_lit(node->ID);
      const float y = bar.BarRect.Max.y;
      ImDrawList *draw_list = node->HostWindow->DrawList;
      draw_list->PushClipRect(node->Pos, ImVec2(node->Pos.x + node->Size.x,
                                                node->Pos.y + node->Size.y));
      draw_list->AddRectFilled(
          ImVec2(bar.SeparatorMinX, y - thickness),
          ImVec2(bar.SeparatorMaxX, y),
          styles::color_u32(focused ? styles::MegatoyCol::TabBarLine
                                    : styles::MegatoyCol::TabBarLineDimmed));
      draw_list->PopClipRect();
      draw_hovered_overline(bar, draw_list);
    }
  }
  lit_nodes().clear();
}

} // namespace ui
