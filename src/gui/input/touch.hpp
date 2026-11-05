#pragma once

#include <imgui.h>

#include <cstdint>
#include <span>

namespace ui::touch {

/// A finger on a touch screen, in ImGui's coordinates.
struct Finger {
  std::uint64_t id = 0;
  ImVec2 pos;
};

/// Kept from the SDL events: where each finger is, from down to up.
void finger_down(std::uint64_t id, ImVec2 pos);
void finger_moved(std::uint64_t id, ImVec2 pos);
void finger_up(std::uint64_t id);

std::span<const Finger> fingers();

/**
 * Makes the first finger down the mouse, instead of SDL. Its own version
 * holds the button down for good once the browser loses a finger's lift,
 * which release_all() cannot reach. The finger then presses only once it is
 * clear it means to: see drag_to_scroll().
 */
void drive_mouse();

/**
 * Lifts every finger still down at the next frame, for when the browser says
 * none is left on the screen: the lifts SDL did report are read first.
 */
void release_all();

/**
 * A widget that answers to the fingers themselves takes the one SDL also
 * reports as the mouse, so a drag from it does not scroll too.
 */
void claim(std::uint64_t id);

/// Presses for a finger held still long enough. Called before
/// ImGui::NewFrame().
void before_new_frame();

/**
 * A finger dragged up or down scrolls the window under it, as a touch screen
 * is expected to. A press that a slider has answered by changing its value is
 * left to the slider, and one on a scrollbar, a resize edge or a dock's
 * divider to that; one on a button, a list row or nothing turns into the
 * scroll, and the press is dropped. When the fingers drive the mouse, moving
 * off at once scrolls without pressing at all, a tap clicks, and anything
 * else is dragged after a long press. Called after ImGui::NewFrame().
 */
void drag_to_scroll();

} // namespace ui::touch
