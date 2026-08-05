#pragma once

// Internals shared by the patch browser's two views. Not part of the public
// UI surface -- include only from the patch_selector / patch_*_view sources.

#include "patch_selector.hpp"

#include <array>
#include <string>
#include <string_view>

namespace ui::selector_detail {

/// "-", 1..5 stars, as icon strings. The mini variant fits narrow columns.
extern const std::array<std::string_view, 6> kStarLabels;
extern const std::array<std::string_view, 6> kStarLabelsMini;

std::string to_lower(const std::string &value);
bool contains_case_insensitive(const std::string &haystack,
                               const std::string &needle_lower);

bool entry_passes_star_filter(const patches::PatchEntry &entry,
                              int min_star_rating);
bool entry_matches_query(const patches::PatchEntry &entry,
                         const std::string &query_lower);

/// Tooltip with format, path and metadata for the last drawn item.
void show_patch_tooltip(const patches::PatchEntry &entry);

/// Right-click menu for the last drawn item.
void entry_context_menu(PatchSelectorContext &context,
                        const patches::PatchEntry &entry);

/// The search box, star filter and "Clear filters" row above either view.
void render_filter_bar(PatchSelectorContext &context);

} // namespace ui::selector_detail
