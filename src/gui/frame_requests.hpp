#pragma once

#include <imgui.h>

namespace ui {

namespace detail {
inline int &frame_wanting_next() {
  static int frame = -1;
  return frame;
}
} // namespace detail

/// Asks for the next frame without waiting for input, for something that
/// moves on its own. Called while a frame is being built.
inline void keep_drawing() {
  detail::frame_wanting_next() = ImGui::GetFrameCount();
}

/// Whether the frame drawn last asked for the next one.
inline bool drawing_wanted() {
  return detail::frame_wanting_next() == ImGui::GetFrameCount();
}

} // namespace ui
