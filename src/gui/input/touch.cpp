#include "touch.hpp"

#include "gui/ui_scale.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

namespace ui::touch {
namespace {

std::vector<Finger> &all_fingers() {
  static std::vector<Finger> fingers;
  return fingers;
}

/// The finger down first while none was, which SDL turns into the mouse.
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
/// around it, with room to scroll down.
ImGuiWindow *scrollable(ImGuiWindow *window) {
  while (window != nullptr) {
    if (window->ScrollMax.y > 0.0f) {
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

} // namespace

void finger_down(std::uint64_t id, ImVec2 pos) {
  auto &fingers = all_fingers();
  if (fingers.empty()) {
    primary() = {id, true, false};
    if (mouse_driven()) {
      mouse_at(pos);
      ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
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
  if (mouse_driven() && primary().down && primary().id == id) {
    mouse_at(pos);
  }
}

void finger_up(std::uint64_t id) {
  std::erase_if(all_fingers(),
                [id](const Finger &finger) { return finger.id == id; });
  if (primary().down && primary().id == id) {
    primary().down = false;
    if (mouse_driven()) {
      mouse_lifted();
    }
  }
}

void drive_mouse() { mouse_driven() = true; }

void release_all() {
  all_fingers().clear();
  if (primary().down) {
    primary().down = false;
    if (mouse_driven()) {
      mouse_lifted();
    }
  }
}

std::span<const Finger> fingers() { return all_fingers(); }

void claim(std::uint64_t id) {
  if (primary().down && primary().id == id) {
    primary().claimed = true;
  }
}

void drag_to_scroll() {
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
    if (std::abs(dy) > ui::scale::px(10.0f) && std::abs(dy) > std::abs(dx) &&
        !slider_moved) {
      scrolling = true;
      ImGui::ClearActiveID();
    }
  }
  if (scrolling) {
    ImGui::SetScrollY(target, target->Scroll.y - io.MouseDelta.y);
  }
}

} // namespace ui::touch
