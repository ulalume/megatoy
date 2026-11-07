#pragma once

#include "gui/ui_scale.hpp"

#include <imgui.h>

namespace ui {

/// How much more a button is padded above and below than other frames.
inline float button_extra_padding() { return scale::px(2.0f); }

/// How tall a button stands.
inline float button_height() {
  return ImGui::GetFrameHeight() + button_extra_padding() * 2.0f;
}

/// ImGui::Button, padded more above and below than other frames.
inline bool button(const char *label, const ImVec2 &size = ImVec2(0, 0)) {
  ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding,
                       ImGui::GetStyle().FramePadding.y +
                           button_extra_padding());
  const bool clicked = ImGui::Button(label, size);
  ImGui::PopStyleVar();
  return clicked;
}

} // namespace ui
