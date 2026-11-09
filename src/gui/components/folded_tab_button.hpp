#pragma once

#include "gui/app_window.hpp"
#include "gui/components/centred_glyph.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <optional>

namespace ui {

/// A folded view's button at the left of a dock's tabs, as last drawn.
struct FoldedTabButton {
  bool hovered = false;
  /// Under the pointer, even while a popup blocks it.
  bool under_pointer = false;
  ImVec2 min;
  ImVec2 max;
};

/**
 * Draws a folded view's button, with its icon and its title as a tooltip, at
 * the left of `node`'s tabs. A button added to a dock's tab bar does not take
 * clicks itself, so the caller reads them from what this returns. Nothing
 * when the node has no tab bar to add to.
 */
inline std::optional<FoldedTabButton> folded_tab_button(ImGuiDockNode *node,
                                                        const char *id,
                                                        const char *icon,
                                                        const char *title) {
  if (node == nullptr || !ImGui::DockNodeBeginAmendTabBar(node)) {
    return std::nullopt;
  }
  // As tall as the tabs beside it, and as wide as they and the line under
  // them are tall.
  const float width = tab_height() + scale::px(kTabLineWidth);
  push_window_tab_padding();
  ImGui::SetNextItemWidth(width);
  ImGui::TabItemButton(id,
                       ImGuiTabItemFlags_Leading | ImGuiTabItemFlags_NoTooltip);
  ImGui::PopStyleVar();
  draw_centred_glyph(icon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  FoldedTabButton button;
  button.hovered = ImGui::IsItemHovered();
  button.under_pointer =
      ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
  button.min = ImGui::GetItemRectMin();
  button.max = ImGui::GetItemRectMax();
  ImGui::DockNodeEndAmendTabBar();
  if (button.hovered) {
    ImGui::SetTooltip("%s", title);
  }
  return button;
}

/**
 * Whether a press on the button has just been let go over it. `pressed`
 * keeps, from one frame to the next, that the press began on it.
 */
inline bool
folded_tab_button_clicked(const std::optional<FoldedTabButton> &button,
                          bool &pressed) {
  const bool hovered = button && button->hovered;
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    pressed = true;
  }
  if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    return false;
  }
  const bool clicked = pressed && hovered;
  pressed = false;
  return clicked;
}

} // namespace ui
