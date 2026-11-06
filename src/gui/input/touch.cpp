#include "touch.hpp"

#include "gui/frame_requests.hpp"
#include "gui/ui_scale.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <utility>
#include <vector>

namespace ui::touch {
namespace {

using Clock = std::chrono::steady_clock;

/// How far a finger may wander and still tap.
constexpr float kTapSlop = 10.0f;
/// Held still this long, a finger presses.
constexpr auto kPressDelay = std::chrono::milliseconds(150);
/// Held still this long, a press may be dragged rather than scroll, or lifted
/// for the context menu.
constexpr auto kLongPress = std::chrono::milliseconds(450);
/// A finger that stopped this long before it lifted does not flick.
constexpr auto kFlickWindow = std::chrono::milliseconds(80);
/// A flicked scroll slows by this much each second.
constexpr float kCoastDecayPerSecond = 0.05f;
/// Below this, in pixels a second, a flicked scroll stops.
constexpr float kCoastStop = 20.0f;

std::vector<Finger> &all_fingers() {
  static std::vector<Finger> fingers;
  return fingers;
}

/// The finger that is the mouse.
struct Primary {
  std::uint64_t id = 0;
  bool down = false;
  bool claimed = false;
};

Primary &primary() {
  static Primary primary;
  return primary;
}

void mouse_at(ImVec2 pos) {
  ImGuiIO &io = ImGui::GetIO();
  io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
  io.AddMousePosEvent(pos.x, pos.y);
}

/// The button up where the finger left, then the pointer off the screen, so
/// nothing stays lit as if hovered.
void mouse_lifted() {
  ImGuiIO &io = ImGui::GetIO();
  io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
  io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
  io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
}

/// The window a drag from `window` scrolls: it, or the nearest window
/// around it, with room to scroll down -- as the mouse wheel finds it, so
/// one the wheel leaves still, like the patch browser around its list, the
/// finger does too.
ImGuiWindow *scrollable(ImGuiWindow *window) {
  while (window != nullptr) {
    if (window->ScrollMax.y > 0.0f &&
        (window->Flags & ImGuiWindowFlags_NoScrollWithMouse) == 0) {
      return window;
    }
    if ((window->Flags & ImGuiWindowFlags_ChildWindow) == 0) {
      return nullptr;
    }
    window = window->ParentWindow;
  }
  return nullptr;
}

/// Whether the press is on something that is dragged by nature: a slider or
/// a drag field (which claim arrow keys while held), a scrollbar, a window's
/// resize edge or corner, or a dock's divider.
bool dragging_a_control(const ImGuiContext &g) {
  ImGuiWindow *window = g.ActiveIdWindow;
  if (g.ActiveId == 0 || window == nullptr) {
    return false;
  }
  if (g.ActiveIdUsingNavDirMask != 0) {
    return true;
  }
  if ((window->Flags & ImGuiWindowFlags_DockNodeHost) != 0) {
    return true;
  }
  if (g.ActiveId == ImGui::GetWindowScrollbarID(window, ImGuiAxis_X) ||
      g.ActiveId == ImGui::GetWindowScrollbarID(window, ImGuiAxis_Y)) {
    return true;
  }
  for (int i = 0; i < 4; ++i) {
    if (g.ActiveId == ImGui::GetWindowResizeCornerID(window, i) ||
        g.ActiveId == ImGui::GetWindowResizeBorderID(window, ImGuiDir(i))) {
      return true;
    }
  }
  return false;
}

/**
 * How the finger that is the mouse is being read. Nothing reaches ImGui's
 * button until it is clear what the finger means: moving off at once up or
 * down, it scrolls and never presses; across, or held still a moment, it
 * presses; lifted before either, it taps. A press is dragged only by what
 * takes a drag at once -- a slider, a scrollbar -- or after a long press;
 * before that, a move up or down turns it into a scroll, and a press moved
 * and lifted is let go of without a click. A long press lifted where it began
 * opens the context menu. A scroll flicked off coasts to a stop.
 */
struct Gesture {
  enum class Mode {
    Waiting,
    Pressed,
    Scrolling,
    Coasting
  } mode = Mode::Waiting;
  ImVec2 origin;
  ImVec2 pos;
  Clock::time_point down_at;
  /// Pressed and passing the finger's moves on to ImGui.
  bool following = false;
  /// Scrolled by the finger and not yet applied.
  float scroll_by = 0.0f;
  /// How fast the scroll ran over the last frames, in pixels a second.
  float velocity = 0.0f;
  /// The first frame of a scroll catches up with the slop, which is no flick.
  bool scroll_started = false;
  Clock::time_point moved_at;
  ImGuiWindow *target = nullptr;
  bool target_found = false;
};

Gesture &gesture() {
  static Gesture gesture;
  return gesture;
}

/// A press to let go of without a click at the next frame, once ImGui can be
/// told to forget it -- and for a long press, a right click in its place.
/// Kept apart from the gesture, which a new finger starts afresh.
struct PendingRelease {
  bool active = false;
  bool menu = false;
  ImVec2 at;
};

PendingRelease &pending_release() {
  static PendingRelease pending;
  return pending;
}

/// The fingers the browser said were lifted, as they were down then.
std::vector<std::uint64_t> &lift_pending() {
  static std::vector<std::uint64_t> ids;
  return ids;
}

bool moved_beyond_tap(const Gesture &touch) {
  const float limit = ui::scale::px(kTapSlop);
  return std::abs(touch.pos.x - touch.origin.x) > limit ||
         std::abs(touch.pos.y - touch.origin.y) > limit;
}

bool mostly_vertical(const Gesture &touch) {
  return std::abs(touch.pos.y - touch.origin.y) >=
         std::abs(touch.pos.x - touch.origin.x);
}

bool held_long(const Gesture &touch) {
  return Clock::now() - touch.down_at > kLongPress;
}

void press(Gesture &touch) {
  mouse_at(touch.origin);
  ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
  touch.mode = Gesture::Mode::Pressed;
}

void follow(Gesture &touch) {
  touch.following = true;
  mouse_at(touch.pos);
}

void start_scrolling(Gesture &touch) {
  touch.mode = Gesture::Mode::Scrolling;
  touch.scroll_by = touch.pos.y - touch.origin.y;
  touch.velocity = 0.0f;
  touch.scroll_started = false;
  touch.moved_at = Clock::now();
}

/// Scrolls the window under where the finger came down by what it has moved
/// since the last frame.
void apply_scroll(Gesture &touch) {
  if (!touch.target_found) {
    touch.target_found = true;
    ImGuiWindow *hovered = nullptr;
    ImGuiWindow *under_moving = nullptr;
    ImGui::FindHoveredWindowEx(touch.origin, false, &hovered, &under_moving);
    touch.target = scrollable(hovered);
  }
  if (touch.target != nullptr && touch.scroll_by != 0.0f) {
    ImGui::SetScrollY(touch.target, touch.target->Scroll.y - touch.scroll_by);
  }
  touch.scroll_by = 0.0f;
}

/// The finger is lifted.
void end_gesture(Gesture &touch) {
  switch (touch.mode) {
  case Gesture::Mode::Waiting:
    // A tap: the press and the release together, where the finger was.
    press(touch);
    mouse_at(touch.pos);
    mouse_lifted();
    touch = {};
    break;
  case Gesture::Mode::Pressed:
    if (!touch.following && (moved_beyond_tap(touch) || held_long(touch))) {
      pending_release() = {true, !moved_beyond_tap(touch), touch.origin};
    } else {
      mouse_at(touch.pos);
      mouse_lifted();
    }
    touch = {};
    break;
  case Gesture::Mode::Scrolling:
    if (Clock::now() - touch.moved_at < kFlickWindow &&
        std::abs(touch.velocity) > kCoastStop) {
      touch.mode = Gesture::Mode::Coasting;
    } else {
      touch = {};
    }
    break;
  case Gesture::Mode::Coasting:
    break;
  }
}

} // namespace

void finger_down(std::uint64_t id, ImVec2 pos) {
  // A finger playing the soft keyboard leaves the mouse to the next one, so
  // the rest of the window still answers while a note is held.
  if (!primary().down || primary().claimed) {
    auto &touch = gesture();
    if (primary().down && touch.mode == Gesture::Mode::Pressed) {
      mouse_lifted();
    }
    primary() = {id, true, false};
    touch = {};
    touch.origin = touch.pos = pos;
    touch.down_at = Clock::now();
    // Not even hovered: nothing lights up under a finger until it presses.
  }
  all_fingers().push_back({id, pos});
}

void finger_moved(std::uint64_t id, ImVec2 pos) {
  for (auto &finger : all_fingers()) {
    if (finger.id == id) {
      finger.pos = pos;
    }
  }
  if (!primary().down || primary().id != id) {
    return;
  }
  auto &touch = gesture();
  const float step = pos.y - touch.pos.y;
  touch.pos = pos;
  switch (touch.mode) {
  case Gesture::Mode::Waiting:
    if (moved_beyond_tap(touch)) {
      if (mostly_vertical(touch) && !primary().claimed) {
        start_scrolling(touch);
      } else {
        // What it reaches decides at the next frame whether to follow.
        press(touch);
      }
    }
    break;
  case Gesture::Mode::Pressed:
    if (touch.following) {
      mouse_at(pos);
    }
    break;
  case Gesture::Mode::Scrolling:
    touch.scroll_by += step;
    if (step != 0.0f) {
      touch.moved_at = Clock::now();
    }
    break;
  case Gesture::Mode::Coasting:
    break;
  }
}

void finger_up(std::uint64_t id) {
  std::erase_if(all_fingers(),
                [id](const Finger &finger) { return finger.id == id; });
  if (!primary().down || primary().id != id) {
    return;
  }
  primary().down = false;
  end_gesture(gesture());
}

void release_all() {
  for (const auto &finger : all_fingers()) {
    lift_pending().push_back(finger.id);
  }
}

std::span<const Finger> fingers() { return all_fingers(); }

void claim(std::uint64_t id) {
  if (primary().down && primary().id == id) {
    primary().claimed = true;
  }
}

void before_new_frame() {
  // The browser says so before SDL's own lifts are read, so only what those
  // left behind is let go -- not a finger that has come down since.
  for (const auto id : std::exchange(lift_pending(), {})) {
    const auto &fingers = all_fingers();
    if (std::any_of(fingers.begin(), fingers.end(),
                    [id](const Finger &finger) { return finger.id == id; })) {
      finger_up(id);
    }
  }
  if (!primary().down) {
    return;
  }
  auto &touch = gesture();
  if (touch.mode == Gesture::Mode::Waiting &&
      Clock::now() - touch.down_at > kPressDelay) {
    press(touch);
  }
}

void drag_to_scroll() {
  if (!all_fingers().empty() || gesture().mode != Gesture::Mode::Waiting ||
      pending_release().active) {
    keep_drawing();
  }
  ImGuiIO &io = ImGui::GetIO();
  if (auto &pending = pending_release(); pending.active) {
    // The press let go of without a click, then a right click in its place.
    ImGui::ClearActiveID();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    if (pending.menu) {
      mouse_at(pending.at);
      io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
      io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
    }
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    pending = {};
  }

  auto &touch = gesture();
  if (touch.mode == Gesture::Mode::Coasting) {
    touch.scroll_by = touch.velocity * io.DeltaTime;
    touch.velocity *= std::pow(kCoastDecayPerSecond, io.DeltaTime);
    apply_scroll(touch);
    if (std::abs(touch.velocity) < kCoastStop || touch.target == nullptr) {
      touch = {};
    }
    return;
  }
  if (!primary().down) {
    return;
  }
  if (touch.mode == Gesture::Mode::Pressed && !touch.following) {
    const ImGuiContext &g = *GImGui;
    const bool taken = (g.ActiveId != 0 && g.ActiveIdHasBeenEditedBefore) ||
                       dragging_a_control(g);
    const bool moved = moved_beyond_tap(touch);
    if (taken || primary().claimed || (moved && held_long(touch))) {
      follow(touch);
    } else if (moved && mostly_vertical(touch)) {
      // Let go of whatever the press reached, without a click.
      ImGui::ClearActiveID();
      mouse_lifted();
      start_scrolling(touch);
    }
  }
  if (touch.mode == Gesture::Mode::Scrolling) {
    // Measured by the frame: the finger's events come in a batch each frame,
    // so the time between two of them says nothing of its speed.
    if (touch.scroll_started && io.DeltaTime > 0.0f) {
      touch.velocity =
          0.6f * (touch.scroll_by / io.DeltaTime) + 0.4f * touch.velocity;
    }
    touch.scroll_started = true;
    apply_scroll(touch);
  }
}

} // namespace ui::touch
