#pragma once

// Internals of the patch browser, shared between its sources. Not part of the
// public UI surface -- include only from the patch_selector / patch_*_view
// sources.

#include "patch_selector.hpp"
#include "patches/patch_filter.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace ui::selector_detail {


/// Whether the entry offers Rename and Delete, in its menu and by key.
bool entry_can_rename(const PatchSelectorContext &context,
                      const patches::PatchEntry &entry);
bool entry_can_delete(const PatchSelectorContext &context,
                      const patches::PatchEntry &entry);

/// Right-click menu for the last drawn item.
void entry_context_menu(PatchSelectorContext &context,
                        const patches::PatchEntry &entry,
                        bool allow_remove_folder = false);

/// The icon picker, a popup opened at the pointer for the context's
/// pending_icon_picker.
void render_icon_picker(PatchSelectorContext &context);

/// The search box above the tree, and its star menu.
void render_filter_bar(PatchSelectorContext &context);

} // namespace ui::selector_detail
