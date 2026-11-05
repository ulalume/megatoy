#include "touch.hpp"

#include "gui/ui_scale.hpp"

#include <imgui_internal.h>

#include <algorithm>
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

} // namespace

void finger_down(std::uint64_t id, ImVec2 pos) {
  auto &fingers = all_fingers();
  if (fingers.empty()) {
    primary() = {id, true, false};
  }
  fingers.push_back({id, pos});
}

void finger_moved(std::uint64_t id, ImVec2 pos) {
  for (auto &finger : all_fingers()) {
    if (finger.id == id) {
      finger.pos = pos;
    }
  }
}

void finger_up(std::uint64_t id) {
  std::erase_if(all_fingers(),
                [id](const Finger &finger) { return finger.id == id; });
  if (primary().id == id) {
    primary().down = false;
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
