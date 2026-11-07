#pragma once

#include "gui/frame_requests.hpp"

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>

namespace ui::scroll_reveal {

namespace detail {
/// One window scrolls at a time: the last request wins.
struct State {
  ImGuiID window = 0;
  float target = 0.0f;
  /// Where it scrolled last frame, so a scroll by the user can take over.
  float last = 0.0f;
  /// A section just opened: its header, where the header was drawn this
  /// frame, and how many more frames reveal it. Several, since a child that
  /// sizes itself to its contents only has its height a frame or two later.
  ImGuiID section = 0;
  float section_top = 0.0f;
  int section_frames = 0;
};
inline State &state() {
  static State s;
  return s;
}
/// Time constant, in seconds, of the approach to the target.
constexpr float kTimeConstantSec = 0.08f;
} // namespace detail

/// Aims the current window's scroll so `top`..`bottom` (screen space) is in
/// view, the top first when it is taller than the view. Nothing moves when it
/// already is.
inline void request(float top, float bottom) {
  ImGuiWindow *window = ImGui::GetCurrentWindow();
  const ImRect view = window->InnerClipRect;
  const float margin = ImGui::GetStyle().ItemSpacing.y;
  float delta = 0.0f;
  if (top - margin < view.Min.y) {
    delta = top - margin - view.Min.y;
  } else if (bottom + margin > view.Max.y) {
    delta = std::min(bottom + margin - view.Max.y, top - margin - view.Min.y);
  }
  if (delta == 0.0f) {
    return;
  }
  auto &s = detail::state();
  s.window = window->ID;
  // Clamped as it moves rather than here: contents that have just grown
  // only raise the scroll's limit a frame later.
  s.target = std::max(ImGui::GetScrollY() + delta, 0.0f);
  s.last = ImGui::GetScrollY();
}

/// For a section header `id` drawn with its top at `top`: `opened` when a
/// click has just opened it. The section_end() after its contents then
/// brings the whole of it into view.
inline void section_header_drawn(ImGuiID id, float top, bool opened) {
  auto &s = detail::state();
  if (opened) {
    s.section = id;
    s.section_frames = 3;
  }
  if (s.section == id) {
    s.section_top = top;
  }
}

/// After the contents of the section headed `id`.
inline void section_end(ImGuiID id) {
  auto &s = detail::state();
  if (s.section == 0 || s.section != id) {
    return;
  }
  request(s.section_top, ImGui::GetCursorScreenPos().y);
  if (--s.section_frames <= 0) {
    s.section = 0;
  } else {
    keep_drawing();
  }
}

/// One frame of the current window's scroll toward its target, once all of
/// its contents are laid out. A scroll by the user, by wheel or scrollbar,
/// stops it where it is.
inline void step() {
  auto &s = detail::state();
  if (s.window == 0 || s.window != ImGui::GetCurrentWindow()->ID) {
    return;
  }
  const float current = ImGui::GetScrollY();
  if (std::fabs(current - s.last) > 1.0f || ImGui::GetIO().MouseWheel != 0.0f) {
    s.window = 0;
    return;
  }
  const float alpha =
      1.0f - std::exp(-ImGui::GetIO().DeltaTime / detail::kTimeConstantSec);
  const float target = std::min(s.target, ImGui::GetScrollMaxY());
  float next = current + (target - current) * alpha;
  if (std::fabs(target - next) < 0.5f) {
    next = target;
    s.window = 0;
  } else {
    keep_drawing();
  }
  ImGui::SetScrollY(next);
  s.last = next;
}

} // namespace ui::scroll_reveal
