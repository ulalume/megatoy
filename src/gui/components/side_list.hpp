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
  ImGuiStorage *storage = ImGui::GetStateStorage();
  ImGui::PushID(id);
  const ImGuiID key = ImGui::GetID("page");
  ImGui::PopID();
  int chosen = select >= 0 ? select : storage->GetInt(key, 0);
  if (ImGui::BeginChild(id, ImVec2(width, 0.0f))) {
    ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, list_row_spacing());
    // A row's highlight reaches half the spacing above it, the first one's
    // too.
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + list_row_spacing() * 0.5f);
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
    ImGui::PopStyleVar();
  }
  ImGui::EndChild();
  storage->SetInt(key, chosen);

  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const float x = ImGui::GetItemRectMax().x + gap;
  ImGui::GetWindowDrawList()->AddLine(ImVec2(x, ImGui::GetItemRectMin().y),
                                      ImVec2(x, ImGui::GetItemRectMax().y),
                                      ImGui::GetColorU32(ImGuiCol_Separator));
  ImGui::SameLine(0.0f, gap * 2.0f);
  return chosen;
}

} // namespace ui
