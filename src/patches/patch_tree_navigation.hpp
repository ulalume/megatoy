#pragma once

#include "patch_tree_flatten.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace patches {

struct TreeRevealState {
  /// The current patch, and whether the tree has yet to be brought to it. A
  /// patch the tree does not hold is tried again when the tree changes.
  std::string selection;
  bool pending = false;
  std::uint64_t tried_revision = 0;
};

/// Empty optional means no scroll; a value lists directories to open.
std::optional<std::vector<std::string>> reveal_tree_selection(
    TreeRevealState &state, const std::vector<patches::PatchEntry> &tree,
    const std::string &selection, std::uint64_t revision, bool filtering);

/// No value when the row already fits; the caller clamps to its scroll range.
std::optional<float> scroll_for_row(std::size_t index, float list_top,
                                    float height, float view, float scroll);

/// The row itself for a directory, its parent otherwise; npos at top level.
std::size_t drop_folder_index(const std::vector<TreeRow> &rows,
                              std::size_t index);
std::size_t last_row_under(const std::vector<TreeRow> &rows, std::size_t index);

} // namespace patches
