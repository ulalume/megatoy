#include "algorithm_preview.hpp"

#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <imgui.h>

namespace ui {
namespace {

struct Cell {
  int column;
  int row;
};

struct Link {
  int from;
  int to;
};

/// Where OP1 to OP4 stand on a grid, what feeds what, and which reach the
/// output: megamml's layouts, with 5 and 6 laid on their side so the three
/// carriers stand in a row under OP1.
struct Layout {
  std::array<Cell, 4> cells;
  std::array<bool, 4> carrier;
  std::array<Link, 3> links;
  int link_count;
  int columns;
  int rows;
};

constexpr std::array<Layout, 8> kLayouts = {{
    {{{{0, 0}, {1, 0}, {2, 0}, {3, 0}}},
     {false, false, false, true},
     {{{0, 1}, {1, 2}, {2, 3}}},
     3,
     4,
     1},
    {{{{0, 0}, {0, 1}, {1, 0}, {2, 0}}},
     {false, false, false, true},
     {{{0, 2}, {1, 2}, {2, 3}}},
     3,
     3,
     2},
    {{{{0, 0}, {0, 1}, {1, 1}, {2, 0}}},
     {false, false, false, true},
     {{{0, 3}, {1, 2}, {2, 3}}},
     3,
     3,
     2},
    {{{{0, 0}, {1, 0}, {1, 1}, {2, 0}}},
     {false, false, false, true},
     {{{0, 1}, {1, 3}, {2, 3}}},
     3,
     3,
     2},
    {{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}},
     {false, true, false, true},
     {{{0, 1}, {2, 3}, {0, 0}}},
     2,
     2,
     2},
    {{{{1, 0}, {0, 1}, {1, 1}, {2, 1}}},
     {false, true, true, true},
     {{{0, 1}, {0, 2}, {0, 3}}},
     3,
     3,
     2},
    {{{{0, 0}, {0, 1}, {1, 1}, {2, 1}}},
     {false, true, true, true},
     {{{0, 1}, {0, 0}, {0, 0}}},
     1,
     3,
     2},
    {{{{0, 0}, {1, 0}, {2, 0}, {3, 0}}},
     {true, true, true, true},
     {{{0, 0}, {0, 0}, {0, 0}}},
     0,
     4,
     1},
}};

} // namespace

void algorithm_diagram(int algorithm, ImVec2 size) {
  const Layout &layout = kLayouts[static_cast<std::size_t>(
      std::clamp(algorithm, 0, static_cast<int>(kLayouts.size()) - 1))];
  const ImVec2 min = ImGui::GetCursorScreenPos();
  ImGui::Dummy(size);
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  const ImVec2 max(min.x + size.x, min.y + size.y);
  draw_list->AddRect(min, max,
                     styles::color_u32(styles::MegatoyCol::PreviewBorder));

  const float pad = ui::scale::px(4.0f);
  const float out_width = ui::scale::px(10.0f);
  // Carriers that stand in one row share an output line under it.
  int carrier_row = -1;
  int carrier_count = 0;
  bool one_carrier_row = true;
  for (std::size_t i = 0; i < 4; ++i) {
    if (!layout.carrier[i]) {
      continue;
    }
    ++carrier_count;
    if (carrier_row >= 0 && carrier_row != layout.cells[i].row) {
      one_carrier_row = false;
    }
    carrier_row = layout.cells[i].row;
  }
  const bool shared_row_output = one_carrier_row && carrier_count > 1;
  // That line runs this far under the carriers' boxes.
  const float drop = shared_row_output ? ui::scale::px(5.0f) : 0.0f;
  const float min_gap = ui::scale::px(6.0f);
  // Boxes no taller than a line of text with room around it, and rows at
  // least min_gap apart; the rows then spread up to half a box apart and the
  // whole sits in the middle of the box.
  const float avail_h = size.y - pad * 2.0f;
  const float rows = static_cast<float>(layout.rows);
  const float box_h =
      std::min(ImGui::GetFontSize() + ui::scale::px(4.0f),
               (avail_h - drop - min_gap * (rows - 1.0f)) / rows);
  const float gap =
      layout.rows > 1
          ? std::clamp((avail_h - drop - box_h * rows) / (rows - 1.0f), min_gap,
                       box_h * 0.5f)
          : 0.0f;
  const float content_h = box_h * rows + gap * (rows - 1.0f) + drop;
  const float cell_w = (size.x - pad * 2.0f - out_width) / layout.columns;
  const float box_w = std::min(cell_w * 0.7f, box_h * 1.8f);
  const float origin_x =
      min.x + pad +
      (size.x - pad * 2.0f - out_width - cell_w * layout.columns) * 0.5f;
  const float origin_y = min.y + pad + (avail_h - content_h) * 0.5f;

  std::array<ImVec2, 4> centres;
  for (std::size_t i = 0; i < 4; ++i) {
    centres[i] =
        ImVec2(std::round(origin_x + (layout.cells[i].column + 0.5f) * cell_w),
               std::round(origin_y + layout.cells[i].row * (box_h + gap) +
                          box_h * 0.5f));
  }

  const ImU32 line = styles::color_u32(styles::MegatoyCol::TextMuted);
  const ImU32 carrier = styles::color_u32(styles::MegatoyCol::CarrierBadge);
  const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
  const float thickness = ui::scale::px(1.0f);

  // A modulator feeds an operator below it from its bottom edge, and any
  // other from its right edge.
  for (int i = 0; i < layout.link_count; ++i) {
    const auto from_index = static_cast<std::size_t>(layout.links[i].from);
    const auto to_index = static_cast<std::size_t>(layout.links[i].to);
    const ImVec2 from = centres[from_index];
    const ImVec2 to = centres[to_index];
    if (layout.cells[to_index].row > layout.cells[from_index].row) {
      draw_list->AddLine(ImVec2(from.x, from.y + box_h * 0.5f),
                         ImVec2(to.x, to.y - box_h * 0.5f), line, thickness);
    } else {
      draw_list->AddLine(ImVec2(from.x + box_w * 0.5f, from.y),
                         ImVec2(to.x - box_w * 0.5f, to.y), line, thickness);
    }
  }

  const float out_x = origin_x + cell_w * layout.columns + out_width * 0.5f;
  if (shared_row_output) {
    // Carriers side by side drop to a line under them that runs out.
    const float merge_y =
        std::round(origin_y + carrier_row * (box_h + gap) + box_h + drop);
    float first_x = max.x;
    for (std::size_t i = 0; i < 4; ++i) {
      if (!layout.carrier[i]) {
        continue;
      }
      first_x = std::min(first_x, centres[i].x);
      draw_list->AddLine(ImVec2(centres[i].x, centres[i].y + box_h * 0.5f),
                         ImVec2(centres[i].x, merge_y), carrier, thickness);
    }
    draw_list->AddLine(ImVec2(first_x, merge_y), ImVec2(out_x, merge_y),
                       carrier, thickness);
  } else {
    for (std::size_t i = 0; i < 4; ++i) {
      if (layout.carrier[i]) {
        draw_list->AddLine(ImVec2(centres[i].x + box_w * 0.5f, centres[i].y),
                           ImVec2(out_x, centres[i].y), carrier, thickness);
      }
    }
  }

  // The numbers at a size that fits the boxes, centred by their glyphs'
  // ink rather than the line they sit on.
  const float font_size = std::min(ImGui::GetFontSize(), box_h * 0.9f);
  ImFont *font = ImGui::GetFont();
  ImFontBaked *baked = font->GetFontBaked(font_size);
  const ImU32 box_fill = ImGui::GetColorU32(ImGuiCol_WindowBg);
  for (std::size_t i = 0; i < 4; ++i) {
    const ImVec2 box_min(centres[i].x - box_w * 0.5f,
                         centres[i].y - box_h * 0.5f);
    const ImVec2 box_max(centres[i].x + box_w * 0.5f,
                         centres[i].y + box_h * 0.5f);
    // A carrier is filled in its colour, a modulator outlined.
    if (layout.carrier[i]) {
      draw_list->AddRectFilled(box_min, box_max, carrier, ui::scale::px(2.0f));
    } else {
      draw_list->AddRectFilled(box_min, box_max, box_fill, ui::scale::px(2.0f));
      draw_list->AddRect(box_min, box_max, line, ui::scale::px(2.0f), 0,
                         thickness);
    }
    const char label[] = {static_cast<char>('1' + i), '\0'};
    const ImFontGlyph *glyph = baked->FindGlyph(static_cast<ImWchar>(label[0]));
    draw_list->AddText(
        font, font_size,
        ImVec2(std::round(centres[i].x - (glyph->X0 + glyph->X1) * 0.5f),
               std::round(centres[i].y - (glyph->Y0 + glyph->Y1) * 0.5f)),
        text, label);
  }
}

} // namespace ui
