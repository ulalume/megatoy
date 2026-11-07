#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace ui {

/// The ID named_slider_int() gives the slider it draws for `label`.
inline ImGuiID named_slider_id(const char *label) {
  ImGui::PushID(label);
  const ImGuiID id = ImGui::GetID("##slider");
  ImGui::PopID();
  return id;
}

/// How wide a named slider must be to show its name and `value_text` whole.
inline float named_slider_fit_width(const char *label, const char *value_text) {
  const ImGuiStyle &style = ImGui::GetStyle();
  return ImGui::CalcTextSize(label, nullptr, true).x +
         ImGui::CalcTextSize(value_text).x + style.FramePadding.x * 2.0f +
         style.ItemInnerSpacing.x * 2.0f;
}

/**
 * A horizontal slider with its name inside it, at the left, and its value at
 * the right, rather than the name beside it. `label` is read as ImGui reads
 * one: the name is what comes before "##", the ID all of it. `format` gives
 * the value's text, as for ImGui::SliderInt.
 */
inline bool named_slider_int(const char *label, int *value, int min, int max,
                             const char *format = "%d",
                             ImGuiSliderFlags flags = 0) {
  if (format == nullptr) {
    format = "%d";
  }
  ImGui::PushID(label);
  // Typed into after a ctrl-click, the slider shows its own text field.
  const bool typing = ImGui::TempInputIsActive(ImGui::GetID("##slider"));
  if (!typing) {
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
  }
  const bool changed =
      ImGui::SliderInt("##slider", value, min, max, format, flags);
  if (!typing) {
    ImGui::PopStyleColor();
  }
  ImGui::PopID();
  if (typing) {
    return changed;
  }

  const ImGuiStyle &style = ImGui::GetStyle();
  const ImVec2 min_corner = ImGui::GetItemRectMin();
  const ImVec2 max_corner = ImGui::GetItemRectMax();
  const float text_y = min_corner.y + style.FramePadding.y;
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);

  char value_text[32];
  ImFormatString(value_text, IM_ARRAYSIZE(value_text), format, *value);
  const float value_x =
      max_corner.x - style.FramePadding.x - ImGui::CalcTextSize(value_text).x;
  draw_list->AddText(ImVec2(value_x, text_y), color, value_text);

  const char *name_end = ImGui::FindRenderedTextEnd(label);
  const float name_right = value_x - style.ItemInnerSpacing.x;
  ImGui::RenderTextEllipsis(
      draw_list, ImVec2(min_corner.x + style.FramePadding.x, text_y),
      ImVec2(name_right, max_corner.y), name_right, label, name_end, nullptr);
  return changed;
}

/**
 * A vertical slider with its value inside at the top and its name inside at
 * the bottom, both centred. `label` is read as for named_slider_int().
 */
inline bool named_vslider_int(const char *label, const ImVec2 &size, int *value,
                              int min, int max) {
  ImGui::PushID(label);
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
  const bool changed = ImGui::VSliderInt("##slider", size, value, min, max);
  ImGui::PopStyleColor();
  ImGui::PopID();

  const ImGuiStyle &style = ImGui::GetStyle();
  const ImVec2 min_corner = ImGui::GetItemRectMin();
  const ImVec2 max_corner = ImGui::GetItemRectMax();
  const float centre_x = (min_corner.x + max_corner.x) * 0.5f;
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);

  char value_text[16];
  ImFormatString(value_text, IM_ARRAYSIZE(value_text), "%d", *value);
  draw_list->AddText(
      ImVec2(IM_ROUND(centre_x - ImGui::CalcTextSize(value_text).x * 0.5f),
             min_corner.y + style.FramePadding.y),
      color, value_text);

  const char *name_end = ImGui::FindRenderedTextEnd(label);
  const ImVec2 name_size = ImGui::CalcTextSize(label, name_end);
  draw_list->AddText(ImVec2(IM_ROUND(centre_x - name_size.x * 0.5f),
                            max_corner.y - style.FramePadding.y - name_size.y),
                     color, label, name_end);
  return changed;
}

} // namespace ui
