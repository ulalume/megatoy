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

/// Tooltip with format, path and metadata for the last drawn item.
void show_patch_tooltip(const patches::PatchRepository &repository,
                        const patches::PatchEntry &entry);

/// Whether the entry offers Rename and Delete, in its menu and by key.
bool entry_can_rename(const PatchSelectorContext &context,
                      const patches::PatchEntry &entry);
bool entry_can_delete(const PatchSelectorContext &context,
                      const patches::PatchEntry &entry);

/// Right-click menu for the last drawn item.
void entry_context_menu(PatchSelectorContext &context,
                        const patches::PatchEntry &entry,
                        bool allow_remove_folder = false);

/// The search box and star filter row above the tree.
void render_filter_bar(PatchSelectorContext &context);

} // namespace ui::selector_detail
