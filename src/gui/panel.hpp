#pragma once

#include "gui/ui_scale.hpp"

#include <imgui.h>

namespace ui {

/// Panels' tabs stand taller than the frames inside them. ImGui sizes a dock
/// node's tab bar from FramePadding when the dock space is updated, and where
/// a docked window's contents start from it at the window's Begin, so both
/// are called with this pushed.
inline void push_panel_tab_padding() {
  ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding,
                       ImGui::GetStyle().FramePadding.y + scale::px(2.0f));
}

/// ImGui::Begin for a panel that docks.
inline bool begin_panel(const char *title, bool *open,
                        ImGuiWindowFlags flags = 0) {
  push_panel_tab_padding();
  const bool visible = ImGui::Begin(title, open, flags);
  ImGui::PopStyleVar();
  return visible;
}

} // namespace ui
