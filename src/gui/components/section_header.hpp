#pragma once

#include "gui/scroll_reveal.hpp"
#include "gui/styles/megatoy_style.hpp"

#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>

namespace ui {

/// A bar that opens and closes the section under it, as wide as the items
/// around it. Returns whether the section is open. Opened, it scrolls into
/// view once section_end() follows its contents.
inline bool section_header(const char *label, bool &open) {
  const ImGuiStyle &style = ImGui::GetStyle();
  const ImVec2 min = ImGui::GetCursorScreenPos();
  const ImVec2 size(std::max(ImGui::GetContentRegionAvail().x, 1.0f),
                    ImGui::GetFrameHeight());
  bool opened = false;
  if (ImGui::InvisibleButton(label, size)) {
    open = !open;
    opened = open;
  }
  scroll_reveal::section_header_drawn(ImGui::GetItemID(), min.y, opened);
  const auto col =
      ImGui::IsItemActive()    ? styles::MegatoyCol::SectionHeaderActive
      : ImGui::IsItemHovered() ? styles::MegatoyCol::SectionHeaderHovered
                               : styles::MegatoyCol::SectionHeader;
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  const ImVec2 max(min.x + size.x, min.y + size.y);
  draw_list->AddRectFilled(min, max, styles::color_u32(col),
                           style.FrameRounding);
  const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
  const float font_size = ImGui::GetFontSize();
  ImGui::RenderArrow(
      draw_list,
      ImVec2(min.x + style.FramePadding.x, min.y + style.FramePadding.y), text,
      open ? ImGuiDir_Down : ImGuiDir_Right, 1.0f);
  draw_list->AddText(ImVec2(min.x + style.FramePadding.x * 2.0f + font_size,
                            min.y + style.FramePadding.y),
                     text, label);
  return open;
}

/// After the contents of the open section headed `label`.
inline void section_end(const char *label) {
  scroll_reveal::section_end(ImGui::GetID(label));
}

} // namespace ui
