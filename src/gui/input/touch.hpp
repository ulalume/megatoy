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
 * Lifts, at the next frame, the fingers down now that SDL has not reported
 * lifting by then: for when the browser says none is left on the screen,
 * which it says before SDL's own lifts are read.
 */
void release_all();

/**
 * A widget that answers to the fingers themselves takes the one that is the
 * mouse, so a drag from it does not scroll, and the next finger down becomes
 * the mouse instead.
 */
void claim(std::uint64_t id);

/// Presses for a finger held still long enough. Called before
/// ImGui::NewFrame().
void before_new_frame();

/**
 * The fingers drive the mouse, in place of SDL's own (SDL_HINT_TOUCH_MOUSE_
 * EVENTS off). Moving off at once up or down scrolls the window under the
 * finger without pressing; a tap clicks; a slider, a scrollbar, a resize edge
 * or a dock's divider is dragged at once; anything else is dragged after a
 * long press, and a long press lifted in place opens the context menu. Called
 * after ImGui::NewFrame(). Keeps frames coming while a finger is down or a
 * gesture runs out.
 */
void drag_to_scroll();

} // namespace ui::touch
