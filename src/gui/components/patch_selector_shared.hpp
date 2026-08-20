#pragma once

// Internals shared by the patch browser's two views. Not part of the public
// UI surface -- include only from the patch_selector / patch_*_view sources.

#include "patch_filter.hpp"
#include "patch_selector.hpp"

#include <array>
#include <string>
#include <string_view>

namespace ui::selector_detail {

/// "-", 1..5 stars, as icon strings. The mini variant fits narrow columns.
extern const std::array<std::string_view, 6> kStarLabels;
extern const std::array<std::string_view, 6> kStarLabelsMini;

/// Tooltip with format, path and metadata for the last drawn item.
void show_patch_tooltip(const patches::PatchEntry &entry);

/// Right-click menu for the last drawn item.
void entry_context_menu(PatchSelectorContext &context,
                        const patches::PatchEntry &entry,
                        bool allow_remove_folder = false);

/// The search box, star filter and "Clear filters" row above either view.
void render_filter_bar(PatchSelectorContext &context);

} // namespace ui::selector_detail
