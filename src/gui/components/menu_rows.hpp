#pragma once

#include "gui/ui_scale.hpp"

#include <algorithm>
#include <cfloat>
#include <imgui.h>

namespace ui {

/// Space between the rows of the patch tree and between the items of a menu,
/// so the two stand as tall as each other.
inline float list_row_spacing() { return scale::px(14.0f); }

/// Space the menu bar's items are laid out with, which makes their highlight
/// stop 6 px short of the bar's top and bottom.
inline float bar_item_spacing() {
  return std::max(ImGui::GetStyle().FramePadding.y * 2.0f - scale::px(12.0f),
                  0.0f);
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

/// How tall a list of `items` rows spaced like list rows stands in a popup.
inline float list_popup_height(int items) {
  return (ImGui::GetFontSize() + list_row_spacing()) * items -
         list_row_spacing() + ImGui::GetStyle().WindowPadding.y * 2.0f;
}

/// BeginCombo with its list spaced like list rows, `max_height` tall before
/// it scrolls: eight rows when 0. Closed by end_combo when it returns true.
inline bool begin_combo(const char *label, const char *preview,
                        ImGuiComboFlags flags = 0, float max_height = 0.0f) {
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(0.0f, 0.0f),
      ImVec2(FLT_MAX, max_height > 0.0f ? max_height : list_popup_height(8)));
  if (!ImGui::BeginCombo(label, preview, flags)) {
    return false;
  }
  ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, list_row_spacing());
  return true;
}

inline void end_combo() {
  ImGui::PopStyleVar();
  ImGui::EndCombo();
}

/// ImGui::Combo over `items`, its list spaced like list rows.
inline bool combo(const char *label, int *current, const char *const items[],
                  int count) {
  const char *preview =
      *current >= 0 && *current < count ? items[*current] : "";
  bool changed = false;
  if (begin_combo(label, preview)) {
    for (int i = 0; i < count; ++i) {
      ImGui::PushID(i);
      const bool selected = i == *current;
      if (ImGui::Selectable(items[i], selected)) {
        *current = i;
        changed = true;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
      ImGui::PopID();
    }
    end_combo();
  }
  return changed;
}

} // namespace ui
