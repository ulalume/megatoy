#pragma once

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
  ImGui::SetNextItemWidth(ImGui::CalcTextSize(icon).x +
                          ImGui::GetStyle().FramePadding.x * 2.0f);
  ImGui::TabItemButton(id,
                       ImGuiTabItemFlags_Leading | ImGuiTabItemFlags_NoTooltip);
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

} // namespace ui
