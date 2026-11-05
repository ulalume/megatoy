#include "touch.hpp"

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
/// A flicked scroll slows by this much each second.
constexpr float kCoastDecayPerSecond = 0.05f;
/// Below this, in pixels a second, a flicked scroll stops.
constexpr float kCoastStop = 20.0f;

std::vector<Finger> &all_fingers() {
  static std::vector<Finger> fingers;
  return fingers;
}

/// The finger down first while none was, which is the mouse.
struct Primary {
  std::uint64_t id = 0;
  bool down = false;
  bool claimed = false;
};

Primary &primary() {
  static Primary primary;
  return primary;
}

bool &mouse_driven() {
  static bool driven = false;
  return driven;
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

/// Whether the press is on something that is dragged by nature: a
/// scrollbar, a window's resize edge or corner, or a dock's divider.
bool dragging_a_control(const ImGuiContext &g) {
  ImGuiWindow *window = g.ActiveIdWindow;
  if (g.ActiveId == 0 || window == nullptr) {
    return false;
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
 * presses; lifted before either, it taps. Once pressed, a move up or down
 * before the long press is over still turns into a scroll, unless what was
 * pressed took it -- a slider whose value moved, a scrollbar -- so dragging
 * anything else starts with a long press, and a long press lifted where it
 * began opens the context menu. A scroll flicked off coasts to a stop.
 */
struct Gesture {
  enum class Mode {
    Waiting,
    Pressed,
    Scrolling,
    Coasting,
    OpeningMenu,
  } mode = Mode::Waiting;
  ImVec2 origin;
  ImVec2 pos;
  Clock::time_point down_at;
  /// Pressed and passing the finger's moves on to ImGui.
  bool following = false;
  /// Scrolled by the finger and not yet applied.
  float scroll_by = 0.0f;
  /// How fast the finger was scrolling, in pixels a second.
  float velocity = 0.0f;
  Clock::time_point moved_at;
  ImGuiWindow *target = nullptr;
  bool target_found = false;
};

Gesture &gesture() {
  static Gesture gesture;
  return gesture;
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
  touch.moved_at = Clock::now();
}

bool held_long(const Gesture &touch) {
  return Clock::now() - touch.down_at > kLongPress;
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

/// Reads the finger SDL itself turns into the mouse, already pressed.
void scroll_from_sdl_mouse() {
  static ImGuiWindow *target = nullptr;
  static ImVec2 start;
  static bool scrolling = false;

  ImGuiContext &g = *GImGui;
  const ImGuiIO &io = g.IO;
  if (!io.MouseDown[ImGuiMouseButton_Left]) {
    target = nullptr;
    scrolling = false;
    return;
  }
  if (io.MouseSource != ImGuiMouseSource_TouchScreen ||
      (primary().down && primary().claimed)) {
    return;
  }
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    target = scrollable(g.HoveredWindow);
    start = io.MousePos;
    scrolling = false;
  }
  if (target == nullptr) {
    return;
  }
  if (!scrolling) {
    const float dx = io.MousePos.x - start.x;
    const float dy = io.MousePos.y - start.y;
    const bool slider_moved = g.ActiveId != 0 && g.ActiveIdHasBeenEditedBefore;
    if (dragging_a_control(g)) {
      target = nullptr;
      return;
    }
    if (std::abs(dy) > ui::scale::px(kTapSlop) && std::abs(dy) > std::abs(dx) &&
        !slider_moved) {
      scrolling = true;
      ImGui::ClearActiveID();
    }
  }
  if (scrolling) {
    ImGui::SetScrollY(target, target->Scroll.y - io.MouseDelta.y);
  }
}

} // namespace

void finger_down(std::uint64_t id, ImVec2 pos) {
  auto &fingers = all_fingers();
  if (fingers.empty()) {
    primary() = {id, true, false};
    if (mouse_driven()) {
      auto &touch = gesture();
      touch = {};
      touch.origin = touch.pos = pos;
      touch.down_at = Clock::now();
      // Not even hovered: nothing lights up under a finger until it presses.
    }
  }
  fingers.push_back({id, pos});
}

void finger_moved(std::uint64_t id, ImVec2 pos) {
  for (auto &finger : all_fingers()) {
    if (finger.id == id) {
      finger.pos = pos;
    }
  }
  if (!mouse_driven() || !primary().down || primary().id != id) {
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
        press(touch);
        follow(touch);
      }
    }
    break;
  case Gesture::Mode::Pressed:
    if (touch.following) {
      mouse_at(pos);
    }
    break;
  case Gesture::Mode::Scrolling: {
    touch.scroll_by += step;
    const auto now = Clock::now();
    const float seconds =
        std::chrono::duration<float>(now - touch.moved_at).count();
    if (seconds > 0.0f) {
      // Smoothed, so one uneven event does not decide the flick.
      touch.velocity = 0.7f * (step / seconds) + 0.3f * touch.velocity;
    }
    touch.moved_at = now;
    break;
  }
  case Gesture::Mode::Coasting:
  case Gesture::Mode::OpeningMenu:
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
  if (!mouse_driven()) {
    return;
  }
  auto &touch = gesture();
  switch (touch.mode) {
  case Gesture::Mode::Waiting:
    // A tap: the press and the release together, where the finger was.
    press(touch);
    mouse_at(touch.pos);
    mouse_lifted();
    touch = {};
    break;
  case Gesture::Mode::Pressed:
    if (!touch.following && held_long(touch)) {
      // Opened at the next frame, once the press is let go of unclicked.
      touch.mode = Gesture::Mode::OpeningMenu;
      break;
    }
    mouse_at(touch.pos);
    mouse_lifted();
    touch = {};
    break;
  case Gesture::Mode::Scrolling:
    // A finger that stopped before it lifted does not flick.
    if (Clock::now() - touch.moved_at < std::chrono::milliseconds(80) &&
        std::abs(touch.velocity) > kCoastStop) {
      touch.mode = Gesture::Mode::Coasting;
    } else {
      touch = {};
    }
    break;
  case Gesture::Mode::Coasting:
  case Gesture::Mode::OpeningMenu:
    break;
  }
}

void drive_mouse() { mouse_driven() = true; }

bool &lift_pending() {
  static bool pending = false;
  return pending;
}

void release_all() { lift_pending() = true; }

std::span<const Finger> fingers() { return all_fingers(); }

void claim(std::uint64_t id) {
  if (primary().down && primary().id == id) {
    primary().claimed = true;
  }
}

void before_new_frame() {
  // The browser says so before SDL's own lifts are read, so only what those
  // left behind is let go.
  if (std::exchange(lift_pending(), false)) {
    all_fingers().clear();
    if (primary().down) {
      primary().down = false;
      if (mouse_driven() && gesture().mode == Gesture::Mode::Pressed) {
        mouse_lifted();
      }
      gesture() = {};
    }
  }
  if (!mouse_driven() || !primary().down) {
    return;
  }
  auto &touch = gesture();
  if (touch.mode == Gesture::Mode::Waiting &&
      Clock::now() - touch.down_at > kPressDelay) {
    press(touch);
  }
}

void drag_to_scroll() {
  if (!mouse_driven()) {
    scroll_from_sdl_mouse();
    return;
  }
  auto &touch = gesture();
  if (touch.mode == Gesture::Mode::OpeningMenu) {
    // The press let go of without a click, then a right click in its place.
    ImGui::ClearActiveID();
    ImGuiIO &io = ImGui::GetIO();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    mouse_at(touch.origin);
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    touch = {};
    return;
  }
  if (touch.mode == Gesture::Mode::Coasting) {
    const float seconds = ImGui::GetIO().DeltaTime;
    touch.scroll_by = touch.velocity * seconds;
    touch.velocity *= std::pow(kCoastDecayPerSecond, seconds);
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
    if (taken || primary().claimed || (moved && !mostly_vertical(touch)) ||
        (moved && held_long(touch))) {
      follow(touch);
    } else if (moved) {
      // Let go of whatever the press reached, without a click.
      ImGui::ClearActiveID();
      mouse_lifted();
      start_scrolling(touch);
    }
  }
  if (touch.mode == Gesture::Mode::Scrolling) {
    apply_scroll(touch);
  }
}

} // namespace ui::touch
