#pragma once

namespace ui {

/// Keeps the line under the tabs of the dock node the current window is in
/// as the focused node's, this frame.
void keep_tab_bar_line_lit();

/**
 * Draws the line under each dock node's tabs in its own colour, over
 * ImGui's, which takes the shown tab's, and a line along the top of the tab
 * under the pointer, which ImGui has none for. Called once the windows are
 * drawn.
 */
void draw_tab_bar_lines();

} // namespace ui
