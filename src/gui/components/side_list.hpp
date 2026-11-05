#pragma once

#include "gui/components/menu_rows.hpp"

#include <imgui.h>
#include <span>

namespace ui {

/**
 * The pages of a panel, listed down its left with rows spaced like the patch
 * tree's, and a line between them and the page. The chosen page is kept in
 * the window's state; `select` chooses one from outside. Leaves the cursor
 * beside the list and returns the page to show.
 */
inline int side_list(const char *id, std::span<const char *const> labels,
                     float width, int select = -1) {
  ImGui::PushID(id);
  int &chosen = *ImGui::GetStateStorage()->GetIntRef(ImGui::GetID("page"), 0);
  ImGui::PopID();
  if (select >= 0) {
    chosen = select;
  }
  if (ImGui::BeginChild(id, ImVec2(width, 0.0f))) {
    ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, list_row_spacing());
    for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
      if (ImGui::Selectable(labels[i], i == chosen)) {
        chosen = i;
      }
    }
    ImGui::PopStyleVar();
  }
  ImGui::EndChild();

  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const float x = ImGui::GetItemRectMax().x + gap;
  ImGui::GetWindowDrawList()->AddLine(ImVec2(x, ImGui::GetItemRectMin().y),
                                      ImVec2(x, ImGui::GetItemRectMax().y),
                                      ImGui::GetColorU32(ImGuiCol_Separator));
  ImGui::SameLine(0.0f, gap * 2.0f);
  return chosen;
}

} // namespace ui
