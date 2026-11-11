#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace ui {

/**
 * ImGui::DockSpaceOverViewport, with the docks' tab bars drawn by megatoy:
 * their background, each tab's shape and the line under them, beneath the
 * labels ImGui draws. The tabs not shown stop short of the line and are
 * rounded all round; the shown one runs down to it.
 */
ImGuiID dock_space_over_viewport(ImGuiID dockspace_id,
                                 const ImGuiViewport *viewport);

/// Where a trailing button among `bar`'s tabs stands: right after them while
/// they leave room, as ImGui lays it out, but moved to the bar's end.
inline float trailing_tab_offset(const ImGuiTabBar &bar,
                                 const ImGuiTabItem &tab) {
  return ImMax(tab.Offset, bar.BarRect.GetWidth() - tab.Width);
}

/// Draws the button among the tabs with id `tab_id` with a fill only while
/// the pointer is over it.
void leave_button_unfilled(ImGuiID tab_id);

/// Keeps the tab bar of the dock node the current window is in drawn as the
/// focused node's.
void keep_tab_bar_lit();

/// Ends the frame's tab bars: sets the labels in the middle of the tabs not
/// shown, and the nodes kept lit in it are drawn so in the next. Called once
/// the windows are drawn.
void finish_tab_bars();

} // namespace ui
