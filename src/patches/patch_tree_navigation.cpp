#include "patch_tree_navigation.hpp"

namespace patches {

std::optional<std::vector<std::string>> reveal_tree_selection(
    TreeRevealState &state, const std::vector<patches::PatchEntry> &tree,
    const std::string &selection, std::uint64_t revision, bool filtering) {
  if (selection != state.selection) {
    state.selection = selection;
    state.pending = !selection.empty();
  } else if (!state.pending || state.tried_revision == revision) {
    return std::nullopt;
  }
  if (!state.pending) {
    return std::nullopt;
  }
  state.tried_revision = revision;
  const auto above = directories_above(tree, selection);
  if (!above) {
    return std::nullopt;
  }
  state.pending = false;
  return filtering ? std::vector<std::string>{} : *above;
}

std::optional<float> scroll_for_row(std::size_t index, float list_top,
                                    float height, float view, float scroll) {
  const float top = list_top + static_cast<float>(index) * height;
  const float bottom = top + height;
  if (top >= scroll && bottom <= scroll + view) {
    return std::nullopt;
  }
  if (bottom <= scroll || top >= scroll + view) {
    return top - (view - height) * 0.5f;
  }
  return top < scroll ? top : bottom - view;
}

/// The folder a drop on row `index` goes into: the row itself when it is a
/// folder, otherwise the folder it is listed under. npos at the top level.
std::size_t drop_folder_index(const std::vector<TreeRow> &rows,
                              std::size_t index) {
  if (rows[index].is_directory) {
    return index;
  }
  for (std::size_t i = index; i-- > 0;) {
    if (rows[i].depth < rows[index].depth) {
      return i;
    }
  }
  return std::string::npos;
}

/// The last row listed under the folder on row `index`, or the row itself.
std::size_t last_row_under(const std::vector<TreeRow> &rows,
                           std::size_t index) {
  std::size_t last = index;
  while (last + 1 < rows.size() && rows[last + 1].depth > rows[index].depth) {
    ++last;
  }
  return last;
}

} // namespace patches
