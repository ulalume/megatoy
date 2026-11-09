#pragma once

#include <imgui.h>

namespace ui {

/**
 * ImGui::DockSpaceOverViewport, with the docks' tab bars drawn by megatoy:
 * their background, each tab's shape and the line under them, beneath the
 * labels ImGui draws. The tabs not shown stand in the middle of the bar,
 * rounded all round; the shown one runs down to the line.
 */
ImGuiID dock_space_over_viewport(ImGuiID dockspace_id,
                                 const ImGuiViewport *viewport);

/// Keeps the tab bar of the dock node the current window is in drawn as the
/// focused node's.
void keep_tab_bar_lit();

/// Ends the frame's tab bars: the nodes kept lit in it are drawn so in the
/// next. Called once the windows are drawn.
void finish_tab_bars();

} // namespace ui
