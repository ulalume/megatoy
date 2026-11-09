#pragma once

#include "gui/ui_scale.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <iterator>

namespace ui {

/// The room a dock's tab bar leaves above its tabs, before the interface's
/// scale. The bar is padded for it; its tabs are drawn as tall as without
/// it, at the bottom, once the frame is drawn.
inline constexpr float kTabRoomAbove = 2.0f;

/// Tabs, a window's own and those of a tab bar inside it, are padded more
/// than other frames, a pixel taller in all, and their bar by
/// kTabRoomAbove more. ImGui sizes a dock node's tab bar from FramePadding
/// when the dock space is updated, where a docked window's contents start
/// from it at the window's Begin, and a tab bar and its tabs from it as each
/// is begun, so all of these are called with this pushed.
inline ImVec2 window_tab_padding() {
  const ImVec2 padding = ImGui::GetStyle().FramePadding;
  return ImVec2(padding.x + scale::px(2.0f),
                padding.y + scale::px(2.5f + kTabRoomAbove * 0.5f));
}

inline void push_window_tab_padding() {
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, window_tab_padding());
}

/// The line under a dock's tabs, before the interface's scale: the style's
/// TabBarBorderSize.
inline constexpr float kTabLineWidth = 2.0f;

/// A dock's tab bar's height, as ImGui lays it out from window_tab_padding().
/// Read outside push_window_tab_padding(), as the measures below are.
inline float tab_bar_height() {
  return ImGui::GetFontSize() + window_tab_padding().y * 2.0f;
}

/// How far above the line under a dock's tabs those not shown stop, before
/// the interface's scale.
inline constexpr float kTabRoomBelow = 2.0f;

/// The height of a dock's tab bar between the room above its tabs and the
/// line under them.
inline float tab_height() {
  return tab_bar_height() - scale::px(kTabRoomAbove + kTabLineWidth);
}

/// How tall the tabs not shown are drawn, stopping kTabRoomBelow above the
/// line.
inline float unshown_tab_height() {
  return tab_height() - scale::px(kTabRoomBelow);
}

/// Where the tabs not shown stand in a dock's tab bar `bar`, from the room
/// above them down to kTabRoomBelow above the line; the shown tab runs
/// kTabRoomBelow further, to the line.
struct TabSpan {
  float top;
  float bottom;
};
inline TabSpan unshown_tab_span(const ImRect &bar) {
  return {bar.Min.y + scale::px(kTabRoomAbove),
          bar.Max.y - ImGui::GetStyle().TabBarBorderSize -
              scale::px(kTabRoomBelow)};
}

/// What ImGui draws a dock's tab bar with, which megatoy draws itself.
inline constexpr ImGuiCol kClearTabBarColors[] = {
    ImGuiCol_Tab,           ImGuiCol_TabHovered,        ImGuiCol_TabSelected,
    ImGuiCol_TabDimmed,     ImGuiCol_TabDimmedSelected, ImGuiCol_TitleBg,
    ImGuiCol_TitleBgActive, ImGuiCol_TitleBgCollapsed};

/// The number of colours push_clear_tab_bar_colors() pushes.
inline constexpr int kClearTabBarColorCount =
    static_cast<int>(std::size(kClearTabBarColors));

/**
 * Makes ImGui's own drawing of a dock's tab bar clear while pushed: its
 * background, the tabs' fills and the line under them. megatoy draws them
 * itself, under ImGui's labels (tab_bar_lines). ImGui draws a dock's tab bar
 * as the dock space is laid out and as its windows begin, so both are called
 * with this pushed. Popped with ImGui::PopStyleColor(kClearTabBarColorCount).
 */
inline void push_clear_tab_bar_colors() {
  for (const ImGuiCol col : kClearTabBarColors) {
    ImGui::PushStyleColor(col, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  }
}

/// ImGui::Begin for one of the app's own windows, the ones that dock.
inline bool begin_window(const char *title, bool *open,
                         ImGuiWindowFlags flags = 0) {
  push_window_tab_padding();
  push_clear_tab_bar_colors();
  const bool visible = ImGui::Begin(title, open, flags);
  ImGui::PopStyleColor(kClearTabBarColorCount);
  ImGui::PopStyleVar();
  return visible;
}

/// ImGui::BeginTabBar and ImGui::BeginTabItem with tabs as tall as a
/// window's.
inline bool begin_tab_bar(const char *id, ImGuiTabBarFlags flags = 0) {
  push_window_tab_padding();
  const bool open = ImGui::BeginTabBar(id, flags);
  ImGui::PopStyleVar();
  return open;
}

inline bool begin_tab_item(const char *label, bool *open = nullptr,
                           ImGuiTabItemFlags flags = 0) {
  push_window_tab_padding();
  const bool selected = ImGui::BeginTabItem(label, open, flags);
  ImGui::PopStyleVar();
  return selected;
}

} // namespace ui
