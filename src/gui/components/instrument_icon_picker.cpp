#include "instrument_icon_picker.hpp"

#include "gui/components/centred_glyph.hpp"
#include "gui/frame_requests.hpp"
#include "gui/instrument_icons.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"

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

/// How much larger than the text the list draws its icons.
constexpr float kListIconScale = 1.75f;
/// The gap between icons, across and down.
constexpr float kCellGap = 2.0f;
/// The gap between rows of groups, and between groups in a row.
constexpr float kRowGap = 6.0f;

/// A group in the list, named as shown: a long name broken in two.
struct ListGroup {
  std::string_view name;
  const char *label;
};

/// The groups the list sets side by side, row by row.
constexpr ListGroup kPiano[] = {{"Piano", "Piano"}, {"Organ", "Organ"}};
constexpr ListGroup kPlucked[] = {
    {"Guitar", "Guitar"}, {"Bass", "Bass"}, {"Strings", "Strings"}};
constexpr ListGroup kVoices[] = {
    {"Ensemble", "Ensemble"}, {"Brass", "Brass"}, {"Voice", "Voice"}};
constexpr ListGroup kWind[] = {{"Reed", "Reed"}, {"Pipe", "Pipe"}};
constexpr ListGroup kSynth[] = {{"Synth", "Synth"}};
constexpr ListGroup kEthnic[] = {{"Ethnic", "Ethnic"}};
constexpr ListGroup kPercussion[] = {
    {"Chromatic Percussion", "Chromatic\nPercussion"},
    {"Percussive", "Percussive"}};
constexpr ListGroup kDrums[] = {{"Drums", "Drums"}};
constexpr ListGroup kEffects[] = {{"Sound Effects", "Sound Effects"}};
constexpr std::span<const ListGroup> kListRows[] = {
    kPiano,  kPlucked,    kVoices, kWind,   kSynth,
    kEthnic, kPercussion, kDrums,  kEffects};

/// One group placed in the list: its icons and the name shown before them.
struct PlacedGroup {
  const instrument_icons::Group *group;
  const char *label;
};

/**
 * The list's rows: kListRows, then any group it leaves out on a row of its
 * own.
 */
const std::vector<std::vector<PlacedGroup>> &list_rows() {
  static const std::vector<std::vector<PlacedGroup>> rows = [] {
    std::vector<std::vector<PlacedGroup>> out;
    std::vector<const instrument_icons::Group *> placed;
    const auto find = [](std::string_view name) {
      for (const auto &group : instrument_icons::groups()) {
        if (group.name == name) {
          return &group;
        }
      }
      return static_cast<const instrument_icons::Group *>(nullptr);
    };
    for (const auto row : kListRows) {
      std::vector<PlacedGroup> line;
      for (const auto &entry : row) {
        if (const auto *group = find(entry.name)) {
          line.push_back({group, entry.label});
          placed.push_back(group);
        }
      }
      if (!line.empty()) {
        out.push_back(std::move(line));
      }
    }
    for (const auto &group : instrument_icons::groups()) {
      if (std::find(placed.begin(), placed.end(), &group) == placed.end()) {
        out.push_back({{&group, group.name.data()}});
      }
    }
    return out;
  }();
  return rows;
}

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
  const float row_gap = scale::px(kRowGap);
  const float label_gap = style.ItemSpacing.x;
  const float group_gap = style.ItemSpacing.x * 3.0f;
  const auto group_width = [&](const char *label, std::size_t icons) {
    return ImGui::CalcTextSize(label).x + label_gap +
           cell * static_cast<float>(icons) +
           gap * static_cast<float>(icons - 1);
  };

  // As wide as the widest row, or as the screen leaves room for.
  float widest = 0.0f;
  for (const auto &row : list_rows()) {
    float row_width = 0.0f;
    for (const auto &placed : row) {
      row_width += (row_width > 0.0f ? group_gap : 0.0f) +
                   group_width(placed.label, placed.group->icons.size());
    }
    widest = std::max(widest, row_width);
  }
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const float width =
      std::min(widest, viewport->WorkSize.x * 0.9f -
                           style.WindowPadding.x * 2.0f - style.ScrollbarSize) +
      style.ScrollbarSize;

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
  if (ImGui::Selectable("None", current.empty(), 0, ImVec2(width, 0.0f))) {
    picked = std::string();
  }

  // The icons scroll below the filter, up to most of the screen's height.
  const float max_height = viewport->WorkSize.y * 0.7f;
  ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f),
                                      ImVec2(width, max_height));
  ImGui::BeginChild("##icons", ImVec2(width, 0.0f),
                    ImGuiChildFlags_AutoResizeY);
  const ImU32 muted =
      ImGui::GetColorU32(styles::color(styles::MegatoyCol::TextMuted));
  // Groups follow one another along a row and go on to the next line where
  // the row is out of room; so do a group's icons, under its first.
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float right = origin.x + width - style.ScrollbarSize;
  float y = origin.y;
  std::vector<const instrument_icons::Icon *> shown;
  for (const auto &row : list_rows()) {
    float x = origin.x;
    bool row_used = false;
    for (const auto &placed : row) {
      shown.clear();
      for (const auto &icon : placed.group->icons) {
        if (instrument_icons::matches(icon.id, filter)) {
          shown.push_back(&icon);
        }
      }
      if (shown.empty()) {
        continue;
      }
      if (row_used && x + group_width(placed.label, shown.size()) > right) {
        x = origin.x;
        y += cell + gap;
      }
      const ImVec2 label = ImGui::CalcTextSize(placed.label);
      ImGui::GetWindowDrawList()->AddText(
          ImVec2(x, y + (cell - label.y) * 0.5f), muted, placed.label);
      const float icons_x = x + label.x + label_gap;
      float icon_x = icons_x;
      for (const auto *icon : shown) {
        if (icon_x > icons_x && icon_x + cell > right) {
          icon_x = icons_x;
          y += cell + gap;
        }
        const std::string id(icon->id);
        ImGui::SetCursorScreenPos(ImVec2(icon_x, y));
        ImGui::PushID(id.c_str());
        if (ImGui::Selectable("##icon", id == current, 0, ImVec2(cell, cell))) {
          picked = id;
        }
        draw_instrument_icon(id, ImGui::GetItemRectMin(),
                             ImGui::GetItemRectMax(), kListIconScale);
        ImGui::SetItemTooltip("%s", instrument_icons::display_name(id).c_str());
        ImGui::PopID();
        icon_x += cell + gap;
      }
      x = icon_x - gap + group_gap;
      row_used = true;
    }
    if (row_used) {
      y += cell + row_gap;
    }
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
