#include "../test_check.hpp"
#include "patches/patch_filter.hpp"
#include "patches/patch_tree_flatten.hpp"

#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

using patches::at_least;
using patches::flatten_visible_rows;
using patches::matching_patch_rows;
using patches::TreeRow;

patches::PatchEntry make_file(const std::string &name,
                              const std::string &relative_path, int stars,
                              const std::string &icon = "") {
  patches::PatchEntry entry;
  entry.name = name;
  entry.relative_path = relative_path;
  entry.format = "dmp";
  entry.is_directory = false;

  patches::PatchMetadata metadata;
  metadata.star_rating = stars;
  metadata.icon = icon;
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
 * banks/            lead.dmp (5 stars, "sawtooth"), fx/ zap.opm (2 stars,
 *                   "percussion")
 * empty/            no patch files, though not empty on disk
 * fresh/            nothing on disk at all
 * solo.dmp          0 stars
 */
std::vector<patches::PatchEntry> make_tree() {
  std::vector<patches::PatchEntry> fx_children;
  fx_children.push_back(
      make_file("zap.opm", "banks/fx/zap.opm", 2, "percussion"));

  std::vector<patches::PatchEntry> bank_children;
  bank_children.push_back(
      make_file("lead.dmp", "banks/lead.dmp", 5, "sawtooth"));
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
  const auto rows = flatten_visible_rows(tree, {});

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

void test_open_directory_exposes_direct_children() {
  const auto tree = make_tree();
  const std::unordered_set<std::string> open{"banks"};
  const auto rows = flatten_visible_rows(tree, open);

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
  const auto nested = flatten_visible_rows(tree, open_nested);
  CHECK(paths_of(nested) ==
        std::vector<std::string>({"banks", "banks/lead.dmp", "banks/fx",
                                  "banks/fx/zap.opm", "fresh", "solo.dmp"}));
  CHECK(nested[3].depth == 2);
  CHECK(!nested[3].is_directory);
}

void test_a_directory_holding_nothing_is_listed() {
  const auto tree = make_tree();

  // Opening it reports the open state and yields no children.
  const auto opened = flatten_visible_rows(tree, {"fresh"});
  CHECK(paths_of(opened) ==
        std::vector<std::string>({"banks", "fresh", "solo.dmp"}));
  CHECK(opened[1].is_open);
}

void test_a_directory_holding_only_an_empty_one_is_listed() {
  // A/ holds nothing but B/, which holds nothing at all.
  std::vector<patches::PatchEntry> a_children;
  a_children.push_back(make_directory("B", "A/B", {}, /*holds_nothing=*/true));
  std::vector<patches::PatchEntry> tree;
  tree.push_back(make_directory("A", "A", std::move(a_children)));

  CHECK(paths_of(flatten_visible_rows(tree, {})) ==
        std::vector<std::string>({"A"}));
  const auto opened = flatten_visible_rows(tree, {"A"});
  CHECK(paths_of(opened) == std::vector<std::string>({"A", "A/B"}));
  CHECK(opened[1].is_directory);
  CHECK(opened[1].depth == 1);

}

void test_empty_tree_is_handled() {
  const std::vector<patches::PatchEntry> tree;
  CHECK(flatten_visible_rows(tree, {}).empty());
}

} // namespace

void test_matching_patches_stand_in_one_list() {
  const auto tree = make_tree();

  // No directory rows, every patch at one depth.
  const auto by_query = matching_patch_rows(tree, "zap", {});
  CHECK(paths_of(by_query) == std::vector<std::string>({"banks/fx/zap.opm"}));
  CHECK(!by_query[0].is_directory);
  CHECK(by_query[0].depth == 1);

  // A folder's name lists what it holds.
  CHECK(paths_of(matching_patch_rows(tree, "banks", {})) ==
        std::vector<std::string>({"banks/lead.dmp", "banks/fx/zap.opm"}));

  // The star filter alone lists every patch rated that high.
  CHECK(paths_of(matching_patch_rows(tree, "", at_least(2))) ==
        std::vector<std::string>({"banks/lead.dmp", "banks/fx/zap.opm"}));
  CHECK(paths_of(matching_patch_rows(tree, "banks", at_least(5))) ==
        std::vector<std::string>({"banks/lead.dmp"}));
  CHECK(matching_patch_rows(tree, "nothing", {}).empty());
}

void test_directories_above_an_entry() {
  patches::PatchEntry inner;
  inner.name = "inner";
  inner.relative_path = "root/inner";
  inner.is_directory = true;
  inner.children.push_back(make_file("lead.dmp", "root/inner/lead.dmp", 0));
  patches::PatchEntry root;
  root.name = "root";
  root.relative_path = "root";
  root.is_directory = true;
  root.children.push_back(inner);
  // A name starting like a directory's is not inside it.
  root.children.push_back(make_file("inner2.dmp", "root/inner2.dmp", 0));
  const std::vector<patches::PatchEntry> tree = {root};

  using patches::directories_above;
  const auto lead = directories_above(tree, "root/inner/lead.dmp");
  CHECK(lead.has_value());
  CHECK((*lead == std::vector<std::string>{"root", "root/inner"}));
  CHECK(directories_above(tree, "root/inner2.dmp") ==
        std::vector<std::string>{"root"});
  CHECK(directories_above(tree, "root")->empty());
  CHECK(!directories_above(tree, "root/inner/missing.dmp").has_value());
  CHECK(!directories_above(tree, "/elsewhere/lead.dmp").has_value());
}

void test_search_key_ignores_surrounding_spaces() {
  using patches::search_key;
  CHECK(search_key("  Acoustic Bass ") == "acoustic bass");
  CHECK(search_key("bass\t") == "bass");
  CHECK(search_key("   ").empty());
  CHECK(search_key("").empty());
}

void test_star_words_ask_for_ratings() {
  using patches::at_least;
  using patches::parse_search;
  using patches::StarFilter;
  const auto rated = parse_search("Bass *4+ Lead");
  CHECK(rated.stars == at_least(4));
  CHECK(rated.text_lower == "bass lead");
  CHECK(parse_search("*1").stars == StarFilter{1u << 1});
  CHECK(parse_search("*1").text_lower.empty());
  CHECK(parse_search("*2 *4 x").stars == StarFilter{(1u << 2) | (1u << 4)});
  CHECK(parse_search("*2 *4 x").text_lower == "x");
  CHECK(parse_search("*1 *3+").stars ==
        StarFilter{at_least(3).ratings | (1u << 1)});
  // Not rating words.
  CHECK(!parse_search("*6").stars.active());
  CHECK(parse_search("*45").text_lower == "*45");
  CHECK(!parse_search("a*4").stars.active());
  CHECK(!parse_search("*4++").stars.active());
  CHECK(parse_search("*").text_lower == "*");
}

void test_least_stars_round_trips() {
  using patches::at_least;
  using patches::least_stars;
  CHECK(!at_least(0).active());
  CHECK(least_stars(at_least(3)) == 3);
  CHECK(least_stars(at_least(1)) == 1);
  CHECK(least_stars({}) == 0);
  CHECK(least_stars(patches::StarFilter{1u << 3}) == 0);
  CHECK(least_stars(patches::StarFilter{1u << 5}) == 5);
}

void test_least_stars_replace_the_rating_words() {
  using patches::with_least_stars;
  CHECK(with_least_stars("", 3) == "*3+ ");
  CHECK(with_least_stars("bass", 3) == "*3+ bass");
  CHECK(with_least_stars("bass *3 lead", 5) == "bass *5+ lead");
  CHECK(with_least_stars("*1 bass *2+", 4) == "*4+ bass ");
  CHECK(with_least_stars("bass *2+ lead", 0) == "bass lead");
}

int main() {
  test_closed_tree_lists_visible_top_level_only();
  test_open_directory_exposes_direct_children();
  test_a_directory_holding_nothing_is_listed();
  test_a_directory_holding_only_an_empty_one_is_listed();
  test_empty_tree_is_handled();
  test_matching_patches_stand_in_one_list();
  test_directories_above_an_entry();
  test_search_key_ignores_surrounding_spaces();
  test_star_words_ask_for_ratings();
  test_least_stars_round_trips();
  test_least_stars_replace_the_rating_words();

  std::cout << "All patch tree flatten tests passed\n";
  return 0;
}
