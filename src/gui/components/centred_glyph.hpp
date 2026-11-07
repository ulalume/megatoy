#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace ui {

/// Draws a one-glyph `icon` centred in the rectangle by the glyph's outline:
/// icon glyphs sit low in their line.
inline void
draw_centred_glyph(const char *icon, ImVec2 min, ImVec2 max,
                   ImU32 color = ImGui::GetColorU32(ImGuiCol_Text)) {
  unsigned int codepoint = 0;
  ImTextCharFromUtf8(&codepoint, icon, nullptr);
  const ImFontGlyph *glyph =
      ImGui::GetFontBaked()->FindGlyph(static_cast<ImWchar>(codepoint));
  if (glyph == nullptr) {
    ImGui::GetWindowDrawList()->AddText(min, color, icon);
    return;
  }
  const ImVec2 centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
  const ImVec2 pos(IM_ROUND(centre.x - (glyph->X0 + glyph->X1) * 0.5f),
                   IM_ROUND(centre.y - (glyph->Y0 + glyph->Y1) * 0.5f));
  ImGui::GetWindowDrawList()->AddText(pos, color, icon);
}

} // namespace ui
