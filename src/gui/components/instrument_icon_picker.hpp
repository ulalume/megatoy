#pragma once

#include <imgui.h>
#include <optional>
#include <string>
#include <string_view>

namespace ui {

/// The colour an icon is drawn in: muted for a patch without one.
ImU32 instrument_icon_color(std::string_view id);

/// Draws the glyph of `id` centred in the rectangle, at `scale` times the
/// text's size, in its colour.
void draw_instrument_icon(std::string_view id, ImVec2 min, ImVec2 max,
                          float scale = 1.0f);

/**
 * A filter, None, and a row of icons for each group that has one matching
 * the filter. The id picked, "" for None, or nothing while none is. For the
 * inside of a popup or a menu.
 */
std::optional<std::string> instrument_icon_list(const std::string &current);

/**
 * instrument_icon_list in a popup opened below the item just drawn, once
 * `popup_id` is opened.
 */
std::optional<std::string> instrument_icon_popup(ImGuiID popup_id,
                                                 const std::string &current);

} // namespace ui
