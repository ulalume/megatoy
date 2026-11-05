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
 * A widget that answers to the fingers themselves takes the one SDL also
 * reports as the mouse, so a drag from it does not scroll too.
 */
void claim(std::uint64_t id);

/**
 * A finger dragged up or down scrolls the window under it, as a touch screen
 * is expected to. A press that a slider has answered by changing its value is
 * left to the slider; one on a button, a list row or nothing turns into the
 * scroll, and the press is dropped. Called after ImGui::NewFrame().
 */
void drag_to_scroll();

} // namespace ui::touch
