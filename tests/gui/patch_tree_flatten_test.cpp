#include "../test_check.hpp"
#include "gui/components/patch_tree_flatten.hpp"

#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

using ui::selector_detail::flatten_visible_rows;
using ui::selector_detail::TreeRow;

patches::PatchEntry make_file(const std::string &name,
                              const std::string &relative_path, int stars,
                              const std::string &category = "") {
  patches::PatchEntry entry;
  entry.name = name;
  entry.relative_path = relative_path;
  entry.format = "dmp";
  entry.is_directory = false;

  patches::PatchMetadata metadata;
  metadata.path = relative_path;
  metadata.star_rating = stars;
  metadata.category = category;
  entry.metadata = metadata;
  return entry;
}

patches::PatchEntry make_directory(const std::string &name,
                                   const std::string &relative_path,
                                   std::vector<patches::PatchEntry> children,
                                   bool holds_nothing = false) {
  patches::PatchEntry entry;
  entry.name = name;
  entry.relative_path = relative_path;
  entry.is_directory = true;
  entry.holds_nothing = holds_nothing;
  entry.children = std::move(children);
  return entry;
}

/**
 * banks/            lead.dmp (5 stars), fx/ zap.opm (2 stars, "percussion")
 * empty/            no patch files, though not empty on disk
 * fresh/            nothing on disk at all
 * solo.dmp          0 stars
 */
std::vector<patches::PatchEntry> make_tree() {
  std::vector<patches::PatchEntry> fx_children;
  fx_children.push_back(
      make_file("zap.opm", "banks/fx/zap.opm", 2, "percussion"));

  std::vector<patches::PatchEntry> bank_children;
  bank_children.push_back(make_file("lead.dmp", "banks/lead.dmp", 5, "lead"));
  bank_children.push_back(
      make_directory("fx", "banks/fx", std::move(fx_children)));

  std::vector<patches::PatchEntry> tree;
  tree.push_back(make_directory("banks", "banks", std::move(bank_children)));
  tree.push_back(make_directory("empty", "empty", {}));
  tree.push_back(make_directory("fresh", "fresh", {}, /*holds_nothing=*/true));
  tree.push_back(make_file("solo.dmp", "solo.dmp", 0));
  return tree;
}

std::vector<std::string> paths_of(const std::vector<TreeRow> &rows) {
  std::vector<std::string> paths;
  paths.reserve(rows.size());
  for (const auto &row : rows) {
    paths.push_back(row.entry->relative_path);
  }
  return paths;
}

void test_closed_tree_lists_visible_top_level_only() {
  const auto tree = make_tree();
  const auto rows = flatten_visible_rows(tree, "", 0, {});

  // `empty` holds no file at any depth, and a directory may stand on its own
  // name only while a query is active -- so it is dropped here. `fresh` holds
  // nothing at all, which lists it while no filter is active.
  CHECK(paths_of(rows) ==
        std::vector<std::string>({"banks", "fresh", "solo.dmp"}));
  CHECK(rows[0].is_directory);
  CHECK(!rows[0].is_open);
  CHECK(rows[0].depth == 0);
  CHECK(rows[1].is_directory);
  CHECK(rows[1].depth == 0);
  CHECK(!rows[2].is_directory);
  CHECK(!rows[2].is_open);
  CHECK(rows[2].depth == 0);
}

void test_closed_ancestors_of_a_deep_match_are_listed() {
  const auto tree = make_tree();

  // Nothing is open, so the only trace of the match two levels down is the
  // top-level row it keeps alive.
  const auto by_query = flatten_visible_rows(tree, "zap", 0, {});
  CHECK(paths_of(by_query) == std::vector<std::string>({"banks"}));
  CHECK(by_query[0].is_directory);
  CHECK(!by_query[0].is_open);
  CHECK(by_query[0].depth == 0);

  // The star filter reaches through a closed directory the same way.
  const auto by_stars = flatten_visible_rows(tree, "", 5, {});
  CHECK(paths_of(by_stars) == std::vector<std::string>({"banks"}));
}

void test_open_directory_exposes_direct_children() {
  const auto tree = make_tree();
  const std::unordered_set<std::string> open{"banks"};
  const auto rows = flatten_visible_rows(tree, "", 0, open);

  CHECK(paths_of(rows) ==
        std::vector<std::string>(
            {"banks", "banks/lead.dmp", "banks/fx", "fresh", "solo.dmp"}));
  CHECK(rows[0].is_open);
  CHECK(rows[1].depth == 1);
  CHECK(rows[2].is_directory);
  CHECK(rows[2].depth == 1);
  CHECK(!rows[2].is_open);
  CHECK(rows[3].depth == 0);

  const std::unordered_set<std::string> open_nested{"banks", "banks/fx"};
  const auto nested = flatten_visible_rows(tree, "", 0, open_nested);
  CHECK(paths_of(nested) ==
        std::vector<std::string>({"banks", "banks/lead.dmp", "banks/fx",
                                  "banks/fx/zap.opm", "fresh", "solo.dmp"}));
  CHECK(nested[3].depth == 2);
  CHECK(!nested[3].is_directory);
}

void test_star_filter_prunes_emptied_directories() {
  const auto tree = make_tree();
  const std::unordered_set<std::string> open{"banks", "banks/fx"};
  const auto rows = flatten_visible_rows(tree, "", 3, open);

  // fx is open but its only patch is below the threshold, so both the
  // directory row and its children go away.
  CHECK(paths_of(rows) ==
        std::vector<std::string>({"banks", "banks/lead.dmp"}));
}

void test_query_keeps_the_ancestors_of_a_match() {
  const auto tree = make_tree();
  const std::unordered_set<std::string> open{"banks"};
  const auto rows = flatten_visible_rows(tree, "zap", 0, open);
  CHECK(paths_of(rows) == std::vector<std::string>({"banks", "banks/fx"}));

  const std::unordered_set<std::string> open_nested{"banks", "banks/fx"};
  const auto nested = flatten_visible_rows(tree, "zap", 0, open_nested);
  CHECK(paths_of(nested) ==
        std::vector<std::string>({"banks", "banks/fx", "banks/fx/zap.opm"}));

  // Categories are searched alongside names.
  const auto by_category =
      flatten_visible_rows(tree, "percussion", 0, open_nested);
  CHECK(paths_of(by_category) ==
        std::vector<std::string>({"banks", "banks/fx", "banks/fx/zap.opm"}));
}

void test_directory_name_match_needs_no_visible_files() {
  const auto tree = make_tree();
  const auto rows = flatten_visible_rows(tree, "empty", 0, {});
  CHECK(paths_of(rows) == std::vector<std::string>({"empty"}));
  CHECK(rows[0].is_directory);
  CHECK(!rows[0].is_open);

  // Opening such a directory reports the open state and still yields no
  // children.
  const auto opened = flatten_visible_rows(tree, "empty", 0, {"empty"});
  CHECK(paths_of(opened) == std::vector<std::string>({"empty"}));
  CHECK(opened[0].is_open);

  // ...but a match on a nested directory does not pull in its parent: the
  // parent is listed only for files below it or a match of its own.
  const std::unordered_set<std::string> open{"banks"};
  const auto nested = flatten_visible_rows(tree, "fx", 0, open);
  CHECK(nested.empty());

  // Opening the matching directory changes nothing: a name-only match never
  // counts as a visible descendant for the ancestors above it.
  const auto nested_open =
      flatten_visible_rows(tree, "fx", 0, {"banks", "banks/fx"});
  CHECK(nested_open.empty());
}

void test_a_directory_holding_nothing_is_listed_only_without_filters() {
  const auto tree = make_tree();

  // Opening it reports the open state and yields no children.
  const auto opened = flatten_visible_rows(tree, "", 0, {"fresh"});
  CHECK(paths_of(opened) ==
        std::vector<std::string>({"banks", "fresh", "solo.dmp"}));
  CHECK(opened[1].is_open);

  // A query it does not match hides it, and so does any star filter.
  CHECK(paths_of(flatten_visible_rows(tree, "solo", 0, {})) ==
        std::vector<std::string>({"solo.dmp"}));
  CHECK(paths_of(flatten_visible_rows(tree, "", 1, {})) ==
        std::vector<std::string>({"banks"}));

  // A query it matches lists it by name, like any other directory.
  CHECK(paths_of(flatten_visible_rows(tree, "fresh", 0, {})) ==
        std::vector<std::string>({"fresh"}));
}

void test_a_directory_holding_only_an_empty_one_is_listed() {
  // A/ holds nothing but B/, which holds nothing at all.
  std::vector<patches::PatchEntry> a_children;
  a_children.push_back(make_directory("B", "A/B", {}, /*holds_nothing=*/true));
  std::vector<patches::PatchEntry> tree;
  tree.push_back(make_directory("A", "A", std::move(a_children)));

  CHECK(paths_of(flatten_visible_rows(tree, "", 0, {})) ==
        std::vector<std::string>({"A"}));
  const auto opened = flatten_visible_rows(tree, "", 0, {"A"});
  CHECK(paths_of(opened) == std::vector<std::string>({"A", "A/B"}));
  CHECK(opened[1].is_directory);
  CHECK(opened[1].depth == 1);

  CHECK(flatten_visible_rows(tree, "zap", 0, {"A"}).empty());
  CHECK(flatten_visible_rows(tree, "", 1, {"A"}).empty());
}

void test_empty_tree_is_handled() {
  const std::vector<patches::PatchEntry> tree;
  CHECK(flatten_visible_rows(tree, "", 0, {}).empty());
  CHECK(flatten_visible_rows(tree, "lead", 5, {"banks"}).empty());
}

} // namespace

int main() {
  test_closed_tree_lists_visible_top_level_only();
  test_closed_ancestors_of_a_deep_match_are_listed();
  test_open_directory_exposes_direct_children();
  test_star_filter_prunes_emptied_directories();
  test_query_keeps_the_ancestors_of_a_match();
  test_directory_name_match_needs_no_visible_files();
  test_a_directory_holding_nothing_is_listed_only_without_filters();
  test_a_directory_holding_only_an_empty_one_is_listed();
  test_empty_tree_is_handled();

  std::cout << "All patch tree flatten tests passed\n";
  return 0;
}
