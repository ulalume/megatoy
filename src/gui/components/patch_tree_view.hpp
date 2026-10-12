#pragma once

// The patch browser's folder-hierarchy view. Internal to the selector.

#include "patch_selector.hpp"

#include <string>
#include <vector>

namespace ui::selector_detail {

/**
 * Render the workspace tree, filtered by search text and minimum stars.
 * Returns true if anything was drawn, so the caller can show an empty-result
 * notice.
 */
bool render_patch_tree(const std::vector<patches::PatchEntry> &tree,
                       PatchSelectorContext &context,
                       const std::string &query_lower, int min_star_rating);

/// Expand a folder of the tree, by its relative path.
void open_tree_directory(const std::string &relative_path);

/// Expand at `to` whatever is expanded at or below `from`, for a folder about
/// to move there. What stays expanded at `from` is harmless if it does move.
void follow_tree_directory(const std::string &from, const std::string &to);

} // namespace ui::selector_detail
