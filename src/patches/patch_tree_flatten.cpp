#include "patch_tree_flatten.hpp"

#include "patch_filter.hpp"

#include <cstddef>

namespace patches {

namespace {

/// Whether anything below keeps the parent listed: a file, or a directory
/// holding nothing on disk, which stands on its own.
bool subtree_keeps_parent(const std::vector<patches::PatchEntry> &items) {
  for (const auto &item : items) {
    if (!item.is_directory || item.holds_nothing ||
        subtree_keeps_parent(item.children)) {
      return true;
    }
  }
  return false;
}

/**
 * Appends one level's visible rows and reports what subtree_keeps_parent()
 * would -- the same answer the parent needs to decide whether it is listed at
 * all, so no directory's subtree is walked twice.
 */
bool collect_level(const std::vector<patches::PatchEntry> &items, int depth,
                   const std::unordered_set<std::string> &open_directories,
                   std::vector<TreeRow> &out) {
  bool keeps_parent = false;
  for (const auto &item : items) {
    if (!item.is_directory) {
      out.push_back(TreeRow{&item, depth, false, false});
      keeps_parent = true;
      continue;
    }
    const bool is_open = open_directories.count(item.relative_path) > 0;
    const std::size_t mark = out.size();
    out.push_back(TreeRow{&item, depth, true, is_open});
    // A closed directory still has to be searched: what lies below it is
    // what decides if its own row is drawn.
    const bool has_children =
        is_open ? collect_level(item.children, depth + 1, open_directories, out)
                : subtree_keeps_parent(item.children);
    if (!has_children && !item.holds_nothing) {
      out.resize(mark);
    }
    keeps_parent = keeps_parent || has_children || item.holds_nothing;
  }
  return keeps_parent;
}

} // namespace

std::vector<TreeRow>
flatten_visible_rows(const std::vector<patches::PatchEntry> &tree,
                     const std::unordered_set<std::string> &open_directories) {
  std::vector<TreeRow> rows;
  collect_level(tree, 0, open_directories, rows);
  return rows;
}

namespace {

void collect_matches(const std::vector<patches::PatchEntry> &items,
                     const std::string &query_lower, StarFilter shown_stars,
                     bool matched_above, std::vector<TreeRow> &out) {
  for (const auto &item : items) {
    const bool matched = matched_above || query_lower.empty() ||
                         entry_matches_query(item, query_lower);
    if (item.is_directory) {
      collect_matches(item.children, query_lower, shown_stars, matched, out);
    } else if (matched && entry_passes_star_filter(item, shown_stars)) {
      out.push_back(TreeRow{&item, 1, false, false});
    }
  }
}

} // namespace

std::vector<TreeRow>
matching_patch_rows(const std::vector<patches::PatchEntry> &tree,
                    const std::string &query_lower, StarFilter shown_stars) {
  std::vector<TreeRow> rows;
  collect_matches(tree, query_lower, shown_stars, false, rows);
  return rows;
}

std::optional<std::vector<std::string>>
directories_above(const std::vector<patches::PatchEntry> &tree,
                  const std::string &relative_path) {
  std::vector<std::string> above;
  const auto *entries = &tree;
  while (entries != nullptr) {
    const std::vector<patches::PatchEntry> *below = nullptr;
    for (const auto &entry : *entries) {
      if (entry.relative_path == relative_path) {
        return above;
      }
      if (relative_path.rfind(entry.relative_path + "/", 0) == 0) {
        above.push_back(entry.relative_path);
        below = &entry.children;
        break;
      }
    }
    entries = below;
  }
  return std::nullopt;
}

} // namespace patches
