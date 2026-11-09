#include "gui/tab_bar_lines.hpp"

#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <vector>

namespace ui {
namespace {

/// How far below the other tabs' tops the shown tab's starts, before the
/// interface's scale.
constexpr float kShownTabDrop = 2.0f;

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

/// `bar`'s shown tab, or nothing.
const ImGuiTabItem *shown_tab(const ImGuiTabBar &bar) {
  for (const ImGuiTabItem &tab : bar.Tabs) {
    if (tab.ID != 0 && tab.ID == bar.VisibleTabId) {
      return &tab;
    }
  }
  return nullptr;
}

bool is_pointed_at(const ImGuiTabItem &tab) {
  const ImGuiContext &g = *GImGui;
  return g.HoveredId == tab.ID || g.ActiveId == tab.ID;
}

/**
 * Starts the fill of `bar`'s shown tab kShownTabDrop lower, its bottom where
 * it was: ImGui draws it as it draws the others, so its vertices are found
 * by the colours ImGui fills a shown tab with, inside the tab, and moved.
 * The label, drawn in other colours, stays.
 */
void drop_shown_tab(const ImGuiTabBar &bar, const ImGuiTabItem &tab,
                    ImDrawList *draw_list) {
  const ImGuiContext &g = *GImGui;
  const float left = bar.BarRect.Min.x + tab.Offset - bar.ScrollingAnim;
  const float right = left + tab.Width;
  const float top = bar.BarRect.Min.y;
  const float bottom = bar.BarRect.Max.y - g.Style.TabBarBorderSize;
  const float height = bottom - top;
  const float drop = ui::scale::px(kShownTabDrop);
  if (height <= drop) {
    return;
  }
  constexpr ImU32 kRgb = ~IM_COL32_A_MASK;
  const ImU32 fills[] = {ImGui::GetColorU32(ImGuiCol_TabSelected) & kRgb,
                         ImGui::GetColorU32(ImGuiCol_TabDimmedSelected) & kRgb,
                         ImGui::GetColorU32(ImGuiCol_TabHovered) & kRgb};
  const float scale = (height - drop) / height;
  for (ImDrawVert &vert : draw_list->VtxBuffer) {
    if (vert.pos.x < left - 1.0f || vert.pos.x > right + 1.0f ||
        vert.pos.y < top - 1.0f || vert.pos.y > bottom) {
      continue;
    }
    const ImU32 rgb = vert.col & kRgb;
    if (rgb == fills[0] || rgb == fills[1] || rgb == fills[2]) {
      vert.pos.y = bottom - (bottom - vert.pos.y) * scale;
    }
  }
}

} // namespace

void keep_tab_bar_line_lit() {
  if (const ImGuiDockNode *node = ImGui::GetCurrentWindow()->DockNode) {
    lit_nodes().push_back(node->ID);
  }
}

void draw_tab_bars() {
  const ImGuiContext &g = *GImGui;
  const float thickness = g.Style.TabBarBorderSize;
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
    ImDrawList *draw_list = node->HostWindow->DrawList;
    const ImGuiTabItem *shown = shown_tab(bar);
    if (shown != nullptr) {
      drop_shown_tab(bar, *shown, draw_list);
    }
    if (thickness <= 0.0f) {
      continue;
    }
    // The line takes the shown tab's colour while it is pointed at.
    const bool focused =
        (bar.Flags & ImGuiTabBarFlags_IsFocused) != 0 || is_lit(node->ID);
    const ImU32 color =
        shown != nullptr && is_pointed_at(*shown)
            ? ImGui::GetColorU32(ImGuiCol_TabHovered)
            : styles::color_u32(focused ? styles::MegatoyCol::TabBarLine
                                        : styles::MegatoyCol::TabBarLineDimmed);
    const float y = bar.BarRect.Max.y;
    draw_list->PushClipRect(node->Pos, ImVec2(node->Pos.x + node->Size.x,
                                              node->Pos.y + node->Size.y));
    draw_list->AddRectFilled(ImVec2(bar.SeparatorMinX, y - thickness),
                             ImVec2(bar.SeparatorMaxX, y), color);
    draw_list->PopClipRect();
  }
  lit_nodes().clear();
}

} // namespace ui
