#include "instrument_icon_picker.hpp"

#include "gui/components/centred_glyph.hpp"
#include "gui/instrument_icons.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"

#include <IconsFontAwesome7.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>
#include <vector>

namespace ui {
namespace {

/// How much larger than the text the list draws its icons.
constexpr float kListIconScale = 1.75f;
/// The most icons in a row: the largest group fits in one.
constexpr int kMaxColumns = 12;
/// The gap between icons, across and down.
constexpr float kCellGap = 2.0f;

} // namespace

ImU32 instrument_icon_color(std::string_view id) {
  return instrument_icons::known(id)
             ? ImGui::GetColorU32(ImGuiCol_Text)
             : ImGui::GetColorU32(styles::color(styles::MegatoyCol::TextMuted));
}

void draw_instrument_icon(std::string_view id, ImVec2 min, ImVec2 max,
                          float scale) {
  ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * scale);
  draw_centred_glyph(instrument_icons::glyph(id), min, max,
                     instrument_icon_color(id));
  ImGui::PopFont();
}

std::optional<std::string> instrument_icon_list(const std::string &current) {
  const ImGuiStyle &style = ImGui::GetStyle();
  const float cell = std::round(ImGui::GetFontSize() * kListIconScale +
                                style.FramePadding.y * 2.0f);
  const float gap = scale::px(kCellGap);
  float label_width = 0.0f;
  for (const auto &group : instrument_icons::groups()) {
    label_width = std::max(
        label_width, ImGui::CalcTextSize(group.name.data(),
                                         group.name.data() + group.name.size())
                         .x);
  }
  label_width += style.ItemSpacing.x * 2.0f;

  // As many icons in a row as the screen leaves room for, up to the largest
  // group.
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const float room = viewport->WorkSize.x * 0.9f - label_width -
                     style.WindowPadding.x * 2.0f - style.ScrollbarSize;
  const int columns =
      std::clamp(static_cast<int>((room + gap) / (cell + gap)), 1, kMaxColumns);
  const float width =
      label_width + cell * columns + gap * (columns - 1) + style.ScrollbarSize;

  static char filter[32] = "";
  if (ImGui::IsWindowAppearing()) {
    filter[0] = '\0';
    ImGui::SetKeyboardFocusHere();
  }
  ImGui::SetNextItemWidth(width);
  ImGui::InputTextWithHint("##filter", ICON_FA_MAGNIFYING_GLASS " Filter...",
                           filter, sizeof(filter));

  std::optional<std::string> picked;
  if (ImGui::Selectable("None", current.empty(), 0, ImVec2(width, 0.0f))) {
    picked = std::string();
  }

  // The icons scroll below the filter, up to most of the screen's height.
  const float max_height = viewport->WorkSize.y * 0.7f;
  ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f),
                                      ImVec2(width, max_height));
  ImGui::BeginChild("##icons", ImVec2(width, 0.0f),
                    ImGuiChildFlags_AutoResizeY);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
  const ImU32 muted =
      ImGui::GetColorU32(styles::color(styles::MegatoyCol::TextMuted));
  std::vector<const instrument_icons::Icon *> shown;
  for (const auto &group : instrument_icons::groups()) {
    shown.clear();
    for (const auto &icon : group.icons) {
      if (instrument_icons::matches(icon.id, filter)) {
        shown.push_back(&icon);
      }
    }
    if (shown.empty()) {
      continue;
    }
    // The group's name at the left of its first row of icons.
    const ImVec2 row = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(row.x, row.y + (cell - ImGui::GetTextLineHeight()) * 0.5f),
        muted, group.name.data(), group.name.data() + group.name.size());
    const float icons_x = ImGui::GetCursorPosX() + label_width;
    for (std::size_t i = 0; i < shown.size(); ++i) {
      if (i % columns == 0) {
        ImGui::SetCursorPosX(icons_x);
      } else {
        ImGui::SameLine();
      }
      const std::string id(shown[i]->id);
      ImGui::PushID(id.c_str());
      if (ImGui::Selectable("##icon", id == current, 0, ImVec2(cell, cell))) {
        picked = id;
      }
      draw_instrument_icon(id, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                           kListIconScale);
      ImGui::SetItemTooltip("%s", instrument_icons::display_name(id).c_str());
      ImGui::PopID();
    }
  }
  ImGui::PopStyleVar();
  ImGui::EndChild();
  return picked;
}

std::optional<std::string> instrument_icon_popup(ImGuiID popup_id,
                                                 const std::string &current) {
  const ImRect button(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  // As tall as the list: a combo's popup is otherwise held to a few rows.
  ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f),
                                      ImVec2(FLT_MAX, FLT_MAX));
  if (!ImGui::BeginComboPopup(popup_id, button, ImGuiComboFlags_None)) {
    return std::nullopt;
  }
  auto picked = instrument_icon_list(current);
  if (picked) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndCombo();
  return picked;
}

} // namespace ui
