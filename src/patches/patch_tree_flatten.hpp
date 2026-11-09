#pragma once

// Turns the workspace tree into the flat row list the tree view draws. Pure
// data transform -- deliberately free of ImGui so it can be unit tested.

#include "patches/patch_filter.hpp"
#include "patches/patch_repository.hpp"

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace patches {

struct TreeRow {
  const PatchEntry *entry;
  int depth; ///< 0 = workspace root level.
  bool is_directory;
  bool is_open; ///< Directories only.
};

/**
 * The rows visible under the given filters and expansion state, in draw order.
 *
 * A directory is listed when a file anywhere below it passes both filters, or
 * -- while searching -- when the directory itself matches the query. With no
 * filter active, a directory holding nothing on disk is listed as well, and
 * keeps the directories above it listed. Its children follow only when its
 * relative_path is in `open_directories`; while a filter is active, they
 * follow whenever any of them is listed, and only then.
 *
 * The returned rows point into `tree`, so they stay valid only as long as the
 * repository does not rebuild it.
 */
std::vector<TreeRow>
flatten_visible_rows(const std::vector<patches::PatchEntry> &tree,
                     const std::string &query_lower, StarFilter shown_stars,
                     const std::unordered_set<std::string> &open_directories);

/**
 * The patches that pass both filters, as one list in the tree's order with
 * no directory rows, each at depth 1. A patch matches the query by itself or
 * through a directory above it that does, so searching a folder's name lists
 * what it holds.
 *
 * The returned rows point into `tree`, as flatten_visible_rows()'s do.
 */
std::vector<TreeRow>
matching_patch_rows(const std::vector<patches::PatchEntry> &tree,
                    const std::string &query_lower, StarFilter shown_stars);

/**
 * The directories the entry at `relative_path` is listed under, outermost
 * first. Empty optional when no entry has that path.
 */
std::optional<std::vector<std::string>>
directories_above(const std::vector<patches::PatchEntry> &tree,
                  const std::string &relative_path);

} // namespace patches
