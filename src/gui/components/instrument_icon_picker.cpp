#include "instrument_icon_picker.hpp"

#include "gui/components/centred_glyph.hpp"
#include "gui/components/menu_rows.hpp"
#include "gui/frame_requests.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include "patches/instrument_icons.hpp"

#include <IconsFontAwesome7.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>
#include <span>
#include <string_view>
#include <vector>

namespace ui {
namespace {

/// The gap between icons, across and down.
constexpr float kCellGap = 2.0f;
/// The gap between rows.
constexpr float kRowGap = 6.0f;

/// The groups the list sets on one row, under one name.
constexpr std::string_view kPiano[] = {"Piano", "Organ"};
constexpr std::string_view kPlucked[] = {"Guitar", "Bass", "Strings"};
constexpr std::string_view kVoices[] = {"Ensemble", "Brass", "Voice"};
constexpr std::string_view kWind[] = {"Reed", "Pipe"};
constexpr std::string_view kSynth[] = {"Synth"};
constexpr std::string_view kEthnic[] = {"Ethnic"};
constexpr std::string_view kPercussion[] = {"Chromatic Percussion",
                                            "Percussive"};
constexpr std::string_view kDrums[] = {"Drums"};
constexpr std::string_view kEffects[] = {"Sound Effects"};
constexpr std::span<const std::string_view> kListRows[] = {
    kPiano,  kPlucked,    kVoices, kWind,   kSynth,
    kEthnic, kPercussion, kDrums,  kEffects};

/// One row of the list: its groups' names, and their icons in order.
struct ListRow {
  std::string label;
  std::vector<const patches::instrument_icons::Icon *> icons;
};

/**
 * The list's rows: kListRows, then any group it leaves out on a row of its
 * own.
 */
const std::vector<ListRow> &list_rows() {
  static const std::vector<ListRow> rows = [] {
    std::vector<ListRow> out;
    std::vector<const patches::instrument_icons::Group *> placed;
    const auto add = [&](ListRow &row,
                         const patches::instrument_icons::Group &group) {
      row.label += (row.label.empty() ? "" : " / ") + std::string(group.name);
      for (const auto &icon : group.icons) {
        row.icons.push_back(&icon);
      }
      placed.push_back(&group);
    };
    for (const auto names : kListRows) {
      ListRow row;
      for (const auto name : names) {
        for (const auto &group : patches::instrument_icons::groups()) {
          if (group.name == name) {
            add(row, group);
          }
        }
      }
      if (!row.icons.empty()) {
        out.push_back(std::move(row));
      }
    }
    for (const auto &group : patches::instrument_icons::groups()) {
      if (std::find(placed.begin(), placed.end(), &group) == placed.end()) {
        ListRow row;
        add(row, group);
        out.push_back(std::move(row));
      }
    }
    return out;
  }();
  return rows;
}

} // namespace

ImU32 instrument_icon_color(std::string_view id) {
  return patches::instrument_icons::known(id)
             ? ImGui::GetColorU32(ImGuiCol_Text)
             : ImGui::GetColorU32(styles::color(styles::MegatoyCol::TextMuted));
}

void draw_instrument_icon(std::string_view id, ImVec2 min, ImVec2 max,
                          float scale) {
  ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * scale);
  draw_centred_glyph(patches::instrument_icons::glyph(id), min, max,
                     instrument_icon_color(id));
  ImGui::PopFont();
}

std::optional<std::string> instrument_icon_list(const std::string &current) {
  const ImGuiStyle &style = ImGui::GetStyle();
  const float cell = std::round(ImGui::GetFontSize() * kLargeIconScale +
                                style.FramePadding.y * 2.0f);
  const float gap = scale::px(kCellGap);
  const float row_gap = scale::px(kRowGap);

  // The names stand in a column at the left, as wide as the longest group's
  // name; a row naming more than one breaks onto further lines.
  float label_width = 0.0f;
  for (const auto &group : patches::instrument_icons::groups()) {
    label_width = std::max(
        label_width, ImGui::CalcTextSize(group.name.data(),
                                         group.name.data() + group.name.size())
                         .x);
  }
  const float icons_x = label_width + style.ItemSpacing.x * 2.0f;

  // The icons on a grid: as many columns as the longest row has icons, or as
  // the screen leaves room for.
  std::size_t longest = 1;
  for (const auto &row : list_rows()) {
    longest = std::max(longest, row.icons.size());
  }
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const float room = viewport->WorkSize.x * 0.9f - icons_x -
                     style.WindowPadding.x * 2.0f - style.ScrollbarSize;
  const int columns = std::clamp(static_cast<int>((room + gap) / (cell + gap)),
                                 1, static_cast<int>(longest));
  const float width =
      icons_x + cell * columns + gap * (columns - 1) + style.ScrollbarSize;

  static char filter[32] = "";
  // The list takes its height from what it held the frame before, so the
  // first few frames after it opens are drawn without waiting for input.
  static int opened_frame = -1;
  if (ImGui::IsWindowAppearing()) {
    filter[0] = '\0';
    ImGui::SetKeyboardFocusHere();
    opened_frame = ImGui::GetFrameCount();
  }
  if (ImGui::GetFrameCount() - opened_frame < 3) {
    keep_drawing();
  }
  ImGui::SetNextItemWidth(width);
  ImGui::InputTextWithHint("##filter", ICON_FA_MAGNIFYING_GLASS " Filter...",
                           filter, sizeof(filter));

  std::optional<std::string> picked;
  // As tall as a list row.
  ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
  if (ImGui::Selectable(
          "None", current.empty(), 0,
          ImVec2(width, ImGui::GetTextLineHeight() + list_row_spacing()))) {
    picked = std::string();
  }
  ImGui::PopStyleVar();

  // The icons scroll below the filter, up to most of the screen's height.
  const float max_height = viewport->WorkSize.y * 0.7f;
  ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f),
                                      ImVec2(width, max_height));
  ImGui::BeginChild("##icons", ImVec2(width, 0.0f),
                    ImGuiChildFlags_AutoResizeY);
  const ImU32 muted =
      ImGui::GetColorU32(styles::color(styles::MegatoyCol::TextMuted));
  ImFont *font = ImGui::GetFont();
  const float font_size = ImGui::GetFontSize();
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  float y = origin.y;
  std::vector<const patches::instrument_icons::Icon *> shown;
  for (const auto &row : list_rows()) {
    shown.clear();
    for (const auto *icon : row.icons) {
      if (patches::instrument_icons::matches(icon->id, filter)) {
        shown.push_back(icon);
      }
    }
    if (shown.empty()) {
      continue;
    }
    const auto lines = static_cast<int>((shown.size() + columns - 1) / columns);
    const float icons_height = cell * lines + gap * (lines - 1);
    const ImVec2 label =
        font->CalcTextSizeA(font_size, FLT_MAX, label_width, row.label.c_str());
    const float height = std::max(icons_height, label.y);
    ImGui::GetWindowDrawList()->AddText(
        font, font_size, ImVec2(origin.x, y + (height - label.y) * 0.5f), muted,
        row.label.c_str(), nullptr, label_width);
    const float icons_top = y + (height - icons_height) * 0.5f;
    for (std::size_t i = 0; i < shown.size(); ++i) {
      const auto column = static_cast<float>(i % columns);
      const auto line = static_cast<float>(i / columns);
      ImGui::SetCursorScreenPos(
          ImVec2(origin.x + icons_x + column * (cell + gap),
                 icons_top + line * (cell + gap)));
      const std::string id(shown[i]->id);
      ImGui::PushID(id.c_str());
      if (ImGui::Selectable("##icon", id == current, 0, ImVec2(cell, cell))) {
        picked = id;
      }
      draw_instrument_icon(id, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                           kLargeIconScale);
      ImGui::SetItemTooltip(
          "%s", patches::instrument_icons::display_name(id).c_str());
      ImGui::PopID();
    }
    y += height + row_gap;
  }
  ImGui::SetCursorScreenPos(ImVec2(origin.x, y));
  ImGui::Dummy(ImVec2(0.0f, 0.0f));
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
