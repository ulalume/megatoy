#pragma once

#include "gui/ui_scale.hpp"

#include <imgui.h>

namespace ui {

/// Space between the rows of the patch tree and between the items of a menu,
/// so the two stand as tall as each other.
inline float list_row_spacing() { return scale::px(10.0f); }

/// Space the menu bar's items are laid out with, which makes their highlight
/// fill the bar.
inline float bar_item_spacing() {
  return ImGui::GetStyle().FramePadding.y * 2.0f;
}

/// BeginMenu, BeginPopup and BeginPopupContextItem with their items spaced
/// like list rows. Each that returns true is closed by end_menu or end_popup.
inline bool begin_menu(const char *label, bool enabled = true) {
  if (!ImGui::BeginMenu(label, enabled)) {
    return false;
  }
  ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, list_row_spacing());
  return true;
}

inline void end_menu() {
  ImGui::PopStyleVar();
  ImGui::EndMenu();
}

inline bool begin_popup(const char *id) {
  if (!ImGui::BeginPopup(id)) {
    return false;
  }
  ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, list_row_spacing());
  return true;
}

inline bool begin_popup_context_item() {
  if (!ImGui::BeginPopupContextItem(nullptr)) {
    return false;
  }
  ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, list_row_spacing());
  return true;
}

inline void end_popup() {
  ImGui::PopStyleVar();
  ImGui::EndPopup();
}

} // namespace ui
