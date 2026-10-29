#pragma once

// Internals of the patch browser, shared between its sources. Not part of the
// public UI surface -- include only from the patch_selector / patch_*_view
// sources.

#include "patch_filter.hpp"
#include "patch_selector.hpp"

#include <array>
#include <string>
#include <string_view>

namespace ui::selector_detail {

/// "-", 1..5 stars, as icon strings.
extern const std::array<std::string_view, 6> kStarLabels;

/// Tooltip with format, path and metadata for the last drawn item.
void show_patch_tooltip(const patches::PatchEntry &entry);

/// Whether the entry offers Rename and Delete, in its menu and by key.
bool entry_can_rename(const PatchSelectorContext &context,
                      const patches::PatchEntry &entry);
bool entry_can_delete(const PatchSelectorContext &context,
                      const patches::PatchEntry &entry);

/// Right-click menu for the last drawn item.
void entry_context_menu(PatchSelectorContext &context,
                        const patches::PatchEntry &entry,
                        bool allow_remove_folder = false);

/// The search box, star filter and "Clear filters" row above the tree.
void render_filter_bar(PatchSelectorContext &context);

} // namespace ui::selector_detail
