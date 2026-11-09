#include "gui/tab_bar_lines.hpp"

#include "gui/app_window.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"

#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>
#include <vector>

namespace ui {
namespace {

/// The nodes kept lit this frame, and those kept lit the frame before, which
/// the tab bars are drawn from: they are drawn before the windows say.
std::vector<ImGuiID> &lit_now() {
  static std::vector<ImGuiID> nodes;
  return nodes;
}
std::vector<ImGuiID> &lit_before() {
  static std::vector<ImGuiID> nodes;
  return nodes;
}

bool is_pointed_at(ImGuiID id) {
  const ImGuiContext &g = *GImGui;
  return id != 0 && (g.HoveredId == id || g.HoveredIdPreviousFrame == id ||
                     g.ActiveId == id);
}

/// The dock nodes whose tab bars show this frame.
template <typename Visit> void for_each_tab_bar(Visit visit) {
  const ImGuiContext &g = *GImGui;
  for (int i = 0; i < g.DockContext.Nodes.Data.Size; ++i) {
    auto *node =
        static_cast<ImGuiDockNode *>(g.DockContext.Nodes.Data[i].val_p);
    if (node == nullptr || node->TabBar == nullptr ||
        node->HostWindow == nullptr || !node->IsVisible ||
        node->IsHiddenTabBar() || node->IsNoTabBar() ||
        node->LastFrameActive != g.FrameCount) {
      continue;
    }
    visit(*node, *node->TabBar);
  }
}

float tab_left(const ImGuiTabBar &bar, const ImGuiTabItem &tab) {
  return bar.BarRect.Min.x + tab.Offset - bar.ScrollingAnim;
}

/// One node's bar: its background, its tabs and the line under them.
void draw_tab_bar(ImDrawList *draw_list, const ImGuiDockNode &node,
                  const ImGuiTabBar &bar) {
  const ImGuiStyle &style = ImGui::GetStyle();
  const bool focused =
      node.IsFocused || std::find(lit_before().begin(), lit_before().end(),
                                  node.ID) != lit_before().end();
  const ImRect bar_rect(ImVec2(node.Pos.x, bar.BarRect.Min.y),
                        ImVec2(node.Pos.x + node.Size.x, bar.BarRect.Max.y));
  draw_list->PushClipRect(bar_rect.Min, bar_rect.Max, true);
  draw_list->AddRectFilled(
      bar_rect.Min, bar_rect.Max,
      ImGui::GetColorU32(focused ? ImGuiCol_TitleBgActive : ImGuiCol_TitleBg));

  const float line = style.TabBarBorderSize;
  const auto [top, bottom] = unshown_tab_span(bar.BarRect);
  const float shown_bottom = bar.BarRect.Max.y - line;
  draw_list->PushClipRect(bar.BarRect.Min, bar.BarRect.Max, true);
  for (const ImGuiTabItem &tab : bar.Tabs) {
    if (tab.Width <= 0.0f) {
      continue;
    }
    // The shown tab keeps its colour under the pointer; its close button
    // lights up instead, as ImGui draws it.
    const bool shown = tab.ID != 0 && tab.ID == bar.VisibleTabId;
    const bool pointed = !shown && is_pointed_at(tab.ID);
    // A button among the tabs, such as a folded view's, keeps a fill of its
    // own.
    const bool button = (tab.Flags & ImGuiTabItemFlags_Button) != 0;
    const ImU32 color =
        pointed  ? ImGui::GetColorU32(ImGuiCol_TabHovered)
        : button ? styles::color_u32(styles::MegatoyCol::TabButton)
                 : ImGui::GetColorU32(
                       shown ? (focused ? ImGuiCol_TabSelected
                                        : ImGuiCol_TabDimmedSelected)
                             : (focused ? ImGuiCol_Tab : ImGuiCol_TabDimmed));
    const float left = tab_left(bar, tab);
    draw_list->AddRectFilled(
        ImVec2(left, top),
        ImVec2(left + tab.Width, shown ? shown_bottom : bottom), color,
        style.TabRounding,
        shown ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersAll);
  }
  draw_list->PopClipRect();

  if (line > 0.0f) {
    const ImU32 color =
        styles::color_u32(focused ? styles::MegatoyCol::TabBarLine
                                  : styles::MegatoyCol::TabBarLineDimmed);
    draw_list->AddRectFilled(ImVec2(bar.SeparatorMinX, shown_bottom),
                             ImVec2(bar.SeparatorMaxX, bar.BarRect.Max.y),
                             color);
  }
  draw_list->PopClipRect();
}

} // namespace

ImGuiID dock_space_over_viewport(ImGuiID dockspace_id,
                                 const ImGuiViewport *viewport) {
  // As ImGui::DockSpaceOverViewport, but for what is drawn in the window
  // under the dock space once it is laid out.
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::SetNextWindowViewport(viewport->ID);
  char label[32];
  ImFormatString(label, IM_COUNTOF(label), "WindowOverViewport_%08X",
                 viewport->ID);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::Begin(label, nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoDocking |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNavFocus);
  ImGui::PopStyleVar(3);

  // ImGui draws the tab bars clear; they are drawn here, under the dock
  // space's own window, from the bars as they were laid out last.
  push_clear_tab_bar_colors();
  dockspace_id = ImGui::DockSpace(dockspace_id);
  ImGui::PopStyleColor(kClearTabBarColorCount);

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  for_each_tab_bar([&](const ImGuiDockNode &node, const ImGuiTabBar &bar) {
    draw_tab_bar(draw_list, node, bar);
  });
  ImGui::End();
  return dockspace_id;
}

void keep_tab_bar_lit() {
  if (const ImGuiDockNode *node = ImGui::GetCurrentWindow()->DockNode) {
    lit_now().push_back(node->ID);
  }
}

void finish_tab_bars() {
  // ImGui sets a label in the middle of the bar; the tabs not shown stand
  // between the room above them and the room and line below, which differ,
  // and their labels, the shown one's too, are moved to their middle.
  const float shift = (scale::px(kTabRoomAbove - kTabRoomBelow) -
                       GImGui->Style.TabBarBorderSize) *
                      0.5f;
  if (shift != 0.0f) {
    for_each_tab_bar([&](const ImGuiDockNode &node, const ImGuiTabBar &bar) {
      for (ImDrawVert &vert : node.HostWindow->DrawList->VtxBuffer) {
        if (vert.pos.y < bar.BarRect.Min.y - 1.0f ||
            vert.pos.y > bar.BarRect.Max.y) {
          continue;
        }
        for (const ImGuiTabItem &tab : bar.Tabs) {
          const float left = tab_left(bar, tab);
          if (vert.pos.x >= left - 1.0f &&
              vert.pos.x <= left + tab.Width + 1.0f) {
            // A button's icon is set in the middle of its own fill.
            if ((tab.Flags & ImGuiTabItemFlags_Button) == 0) {
              vert.pos.y += shift;
            }
            break;
          }
        }
      }
    });
  }
  lit_before().swap(lit_now());
  lit_now().clear();
}

} // namespace ui
