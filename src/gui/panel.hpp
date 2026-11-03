#pragma once

#include "gui/ui_scale.hpp"

#include <imgui.h>

namespace ui {

/// Tabs, a panel's own and those of a tab bar inside it, stand taller than
/// other frames. ImGui sizes a dock node's tab bar from FramePadding when the
/// dock space is updated, where a docked window's contents start from it at
/// the window's Begin, and a tab bar and its tabs from it as each is begun,
/// so all of these are called with this pushed.
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

/// ImGui::BeginTabBar and ImGui::BeginTabItem with tabs as tall as a
/// panel's.
inline bool begin_tab_bar(const char *id, ImGuiTabBarFlags flags = 0) {
  push_panel_tab_padding();
  const bool open = ImGui::BeginTabBar(id, flags);
  ImGui::PopStyleVar();
  return open;
}

inline bool begin_tab_item(const char *label, bool *open = nullptr,
                           ImGuiTabItemFlags flags = 0) {
  push_panel_tab_padding();
  const bool selected = ImGui::BeginTabItem(label, open, flags);
  ImGui::PopStyleVar();
  return selected;
}

} // namespace ui
