#pragma once

#include "gui/components/menu_rows.hpp"

#include <imgui.h>
#include <span>

namespace ui {

/// A list down a panel's left, `width` wide; every call is closed by
/// end_side_list(). Returns whether it is visible, as BeginChild does.
inline bool begin_side_list(const char *id, float width) {
  return ImGui::BeginChild(id, ImVec2(width, 0.0f));
}

/// Closes the list, draws a line between it and the page, and leaves the
/// cursor beside it.
inline void end_side_list() {
  ImGui::EndChild();
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const float x = ImGui::GetItemRectMax().x + gap;
  ImGui::GetWindowDrawList()->AddLine(ImVec2(x, ImGui::GetItemRectMin().y),
                                      ImVec2(x, ImGui::GetItemRectMax().y),
                                      ImGui::GetColorU32(ImGuiCol_Separator));
  ImGui::SameLine(0.0f, gap * 2.0f);
}

/// Spaces the rows that follow as the patch tree's, the first one's
/// highlight reaching half the spacing above it too. Closed by
/// end_side_list_rows().
inline void begin_side_list_rows() {
  ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, list_row_spacing());
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + list_row_spacing() * 0.5f);
}

inline void end_side_list_rows() { ImGui::PopStyleVar(); }

/**
 * The pages of a panel, listed down its left with rows spaced like the patch
 * tree's, and a line between them and the page. Every panel's list is as
 * wide. The chosen page is kept in the window's state; `select` chooses one
 * from outside. Leaves the cursor beside the list and returns the page to
 * show.
 */
inline int side_list(const char *id, std::span<const char *const> labels,
                     int select = -1) {
  ImGuiStorage *storage = ImGui::GetStateStorage();
  ImGui::PushID(id);
  const ImGuiID key = ImGui::GetID("page");
  ImGui::PopID();
  int chosen = select >= 0 ? select : storage->GetInt(key, 0);
  if (begin_side_list(id, scale::px(90.0f))) {
    begin_side_list_rows();
    // The label drawn in from the row's edge, as a frame's text is.
    const float inset = ImGui::GetStyle().FramePadding.x;
    for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
      ImGui::PushID(i);
      const ImVec2 row = ImGui::GetCursorScreenPos();
      if (ImGui::Selectable("##page", i == chosen)) {
        chosen = i;
      }
      ImGui::GetWindowDrawList()->AddText(ImVec2(row.x + inset, row.y),
                                          ImGui::GetColorU32(ImGuiCol_Text),
                                          labels[i]);
      ImGui::PopID();
    }
    end_side_list_rows();
  }
  end_side_list();
  storage->SetInt(key, chosen);
  return chosen;
}

} // namespace ui
