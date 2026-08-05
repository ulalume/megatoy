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

} // namespace ui::selector_detail
