#pragma once

#include "gui/app_window.hpp"
#include "gui/components/centred_glyph.hpp"
#include "gui/tab_bar_lines.hpp"

#include <algorithm>
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
 * Keeps the buttons at the left of `bar` in the order they are drawn: a
 * dock's tab bar keeps its tabs in the order they first came, so a button
 * that came before one drawn ahead of it this frame moves after that one.
 * Called right after the button is drawn, which the bar keeps as its last
 * tab drawn, moved with it.
 */
inline void keep_leading_order(ImGuiTabBar &bar, ImGuiID id) {
  ImGuiTabItem *tab = ImGui::TabBarFindTabByID(&bar, id);
  if (tab == nullptr) {
    return;
  }
  ImGuiTabItem *after = tab;
  for (ImGuiTabItem *other = tab + 1; other < bar.Tabs.end(); ++other) {
    if ((other->Flags & ImGuiTabItemFlags_Leading) != 0 &&
        other->LastFrameVisible == ImGui::GetFrameCount() &&
        other->BeginOrder < tab->BeginOrder) {
      after = other;
    }
  }
  if (after == tab) {
    return;
  }
  std::rotate(tab, tab + 1, after + 1);
  bar.LastTabItemIdx = static_cast<ImS16>(bar.Tabs.index_from_ptr(after));
}

/**
 * Draws a button with `icon` among `node`'s tabs, its title as a tooltip,
 * `width` wide: at their left with ImGuiTabItemFlags_Leading, at the bar's
 * right end with ImGuiTabItemFlags_Trailing. A button added to a dock's tab bar
 * does not take clicks itself, so the caller reads them from what this returns.
 * Nothing when the node has no tab bar to add to.
 */
inline std::optional<FoldedTabButton>
tab_bar_button(ImGuiDockNode *node, const char *id, const char *icon,
               const char *title, ImGuiTabItemFlags placement, float width) {
  if (node == nullptr || !ImGui::DockNodeBeginAmendTabBar(node)) {
    return std::nullopt;
  }
  ImGuiTabBar *bar = node->TabBar;
  if ((placement & ImGuiTabItemFlags_Trailing) != 0) {
    if (ImGuiTabItem *tab = ImGui::TabBarFindTabByID(bar, ImGui::GetID(id))) {
      tab->Offset = trailing_tab_offset(*bar, *tab);
    }
  }
  push_window_tab_padding();
  push_clear_tab_bar_colors();
  ImGui::SetNextItemWidth(width);
  ImGui::TabItemButton(id, placement | ImGuiTabItemFlags_NoTooltip);
  ImGui::PopStyleColor(kClearTabBarColorCount);
  ImGui::PopStyleVar();
  const ImRect item(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  const bool hovered = ImGui::IsItemHovered();
  const bool under_pointer =
      ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
  if ((placement & ImGuiTabItemFlags_Leading) != 0) {
    keep_leading_order(*bar, ImGui::GetID(id));
  }
  // The icon in the middle of the fill, which stops as far above the line
  // as the tabs not shown do.
  const TabSpan span = unshown_tab_span(bar->BarRect);
  draw_centred_glyph(icon, ImVec2(item.Min.x, span.top),
                     ImVec2(item.Max.x, span.bottom));
  FoldedTabButton button;
  button.hovered = hovered;
  button.under_pointer = under_pointer;
  button.min = item.Min;
  button.max = item.Max;
  ImGui::DockNodeEndAmendTabBar();
  if (button.hovered) {
    ImGui::SetTooltip("%s", title);
  }
  return button;
}

/// A folded view's button at the left of `node`'s tabs, as wide as the tabs
/// beside it are tall, its fill drawn with theirs.
inline std::optional<FoldedTabButton> folded_tab_button(ImGuiDockNode *node,
                                                        const char *id,
                                                        const char *icon,
                                                        const char *title) {
  return tab_bar_button(node, id, icon, title, ImGuiTabItemFlags_Leading,
                        unshown_tab_height());
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

/**
 * Opens the popup `popup_id` when a press on the button is let go, unless
 * the popup was open when the press began: that press closed it, as a press
 * outside it does. `open_at_press` keeps that from one frame to the next.
 */
inline void open_popup_from(const FoldedTabButton &button, ImGuiID popup_id,
                            bool &open_at_press) {
  // Also through the open popup, so a press here while it is open is known
  // for the one that closes it.
  if (!button.under_pointer) {
    return;
  }
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    open_at_press = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);
  }
  // On release, so the press is not taken for one outside the popup.
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !open_at_press) {
    ImGui::OpenPopupEx(popup_id);
  }
}

} // namespace ui
