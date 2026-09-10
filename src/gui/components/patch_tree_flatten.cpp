#include "patch_tree_flatten.hpp"

#include "patch_filter.hpp"

#include <cstddef>

namespace ui::selector_detail {

namespace {

/// A directory holding nothing on disk stands on its own while no filter is
/// active.
bool listed_while_empty(const patches::PatchEntry &directory,
                        const std::string &query_lower, int min_star_rating) {
  return directory.holds_nothing && query_lower.empty() && min_star_rating == 0;
}

/// Whether anything below keeps the parent listed: a file that passes the
/// filters, or a directory listed_while_empty().
bool subtree_keeps_parent(const std::vector<patches::PatchEntry> &items,
                          const std::string &query_lower, int min_star_rating) {
  for (const auto &item : items) {
    if (item.is_directory) {
      if (listed_while_empty(item, query_lower, min_star_rating) ||
          subtree_keeps_parent(item.children, query_lower, min_star_rating)) {
        return true;
      }
    } else if (entry_passes_star_filter(item, min_star_rating) &&
               entry_matches_query(item, query_lower)) {
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
                   const std::string &query_lower, int min_star_rating,
                   const std::unordered_set<std::string> &open_directories,
                   std::vector<TreeRow> &out) {
  bool keeps_parent = false;

  for (const auto &item : items) {
    if (item.is_directory) {
      const bool is_open = open_directories.count(item.relative_path) > 0;
      const std::size_t mark = out.size();
      out.push_back(TreeRow{&item, depth, true, is_open});

      // A closed directory still has to be searched: what lies below it is
      // what decides if its own row is drawn.
      const bool has_children =
          is_open ? collect_level(item.children, depth + 1, query_lower,
                                  min_star_rating, open_directories, out)
                  : subtree_keeps_parent(item.children, query_lower,
                                         min_star_rating);
      const bool empty_on_disk =
          listed_while_empty(item, query_lower, min_star_rating);
      const bool matches_self =
          !query_lower.empty() && entry_matches_query(item, query_lower);
      if (!has_children && !empty_on_disk && !matches_self) {
        out.resize(mark);
      }
      keeps_parent = keeps_parent || has_children || empty_on_disk;
      continue;
    }

    if (!entry_passes_star_filter(item, min_star_rating) ||
        !entry_matches_query(item, query_lower)) {
      continue;
    }
    out.push_back(TreeRow{&item, depth, false, false});
    keeps_parent = true;
  }

  return keeps_parent;
}

} // namespace

std::vector<TreeRow>
flatten_visible_rows(const std::vector<patches::PatchEntry> &tree,
                     const std::string &query_lower, int min_star_rating,
                     const std::unordered_set<std::string> &open_directories) {
  std::vector<TreeRow> rows;
  collect_level(tree, 0, query_lower, min_star_rating, open_directories, rows);
  return rows;
}

} // namespace ui::selector_detail
