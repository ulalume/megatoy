#pragma once

namespace ui {

/// Keeps the line under the tabs of the dock node the current window is in
/// as the focused node's, this frame.
void keep_tab_bar_line_lit();

/**
 * Finishes each dock node's tabs, once the windows are drawn: sets them at
 * the bottom of their bar, below the room it leaves above them, the shown
 * one starting a little below the others, and draws the line under the
 * tabs in its own colour over ImGui's, which takes the shown tab's, or in
 * the shown tab's while it is pointed at.
 */
void draw_tab_bars();

} // namespace ui
