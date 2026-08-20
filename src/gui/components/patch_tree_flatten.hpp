#pragma once

// Turns the workspace tree into the flat row list the tree view draws. Pure
// data transform -- deliberately free of ImGui so it can be unit tested.

#include "patches/patch_repository.hpp"

#include <string>
#include <unordered_set>
#include <vector>

namespace ui::selector_detail {

struct TreeRow {
  const patches::PatchEntry *entry;
  int depth; ///< 0 = workspace root level.
  bool is_directory;
  bool is_open; ///< Directories only.
};

/**
 * The rows visible under the given filters and expansion state, in draw order.
 *
 * A directory is listed when a file anywhere below it passes both filters, or
 * -- while searching -- when the directory itself matches the query. Its
 * children follow only when its relative_path is in `open_directories`.
 *
 * The returned rows point into `tree`, so they stay valid only as long as the
 * repository does not rebuild it.
 */
std::vector<TreeRow>
flatten_visible_rows(const std::vector<patches::PatchEntry> &tree,
                     const std::string &query_lower, int min_star_rating,
                     const std::unordered_set<std::string> &open_directories);

} // namespace ui::selector_detail
