#include "../test_check.hpp"
#include "patches/patch_tree_navigation.hpp"

#include <string>
#include <vector>

namespace {

using namespace ui::selector_detail;

std::vector<patches::PatchEntry> make_tree() {
  patches::PatchEntry patch{.relative_path = "root/sub/lead.gin"};
  patches::PatchEntry sub{
      .relative_path = "root/sub", .is_directory = true, .children = {patch}};
  return {{.relative_path = "root", .is_directory = true, .children = {sub}},
          {.relative_path = "outside.gin"}};
}

void test_reveal() {
  const auto tree = make_tree();
  TreeRevealState state;
  CHECK(!reveal_tree_selection(state, tree, "", 1, false));
  const auto opened =
      reveal_tree_selection(state, tree, "root/sub/lead.gin", 1, false);
  CHECK(opened.has_value());
  CHECK(*opened == (std::vector<std::string>{"root", "root/sub"}));
  CHECK(!state.pending);
  CHECK(!reveal_tree_selection(state, tree, "root/sub/lead.gin", 1, false));
  CHECK(!reveal_tree_selection(state, tree, "root/sub/lead.gin", 2, false));
  const auto top = reveal_tree_selection(state, tree, "outside.gin", 2, false);
  CHECK(top && top->empty());
  const auto filtered =
      reveal_tree_selection(state, tree, "root/sub/lead.gin", 2, true);
  CHECK(filtered && filtered->empty());
  CHECK(!reveal_tree_selection(state, tree, "root/sub/lead.gin", 2, false));
  CHECK(!reveal_tree_selection(state, tree, "", 2, false));
  CHECK(state.selection.empty() && !state.pending);

  CHECK(!reveal_tree_selection(state, {}, "root/sub/lead.gin", 3, false));
  CHECK(state.pending);
  CHECK(!reveal_tree_selection(state, tree, "root/sub/lead.gin", 3, false));
  CHECK(!reveal_tree_selection(state, {}, "root/sub/lead.gin", 4, false));
  const auto retried =
      reveal_tree_selection(state, tree, "root/sub/lead.gin", 5, false);
  CHECK(retried == opened);
  CHECK(!state.pending);

  CHECK(!reveal_tree_selection(state, tree, "missing.gin", 6, false));
  CHECK(state.pending);
  CHECK(reveal_tree_selection(state, tree, "outside.gin", 6, false));
  CHECK(!state.pending);
  CHECK(!reveal_tree_selection(state, {}, "root/sub/lead.gin", 7, true));
  const auto retry_filtered =
      reveal_tree_selection(state, tree, "root/sub/lead.gin", 8, true);
  CHECK(retry_filtered && retry_filtered->empty());
  CHECK(!reveal_tree_selection(state, tree, "missing.gin", 9, false));
  CHECK(!reveal_tree_selection(state, tree, "", 9, false));
  CHECK(!state.pending);
}

void test_scroll() {
  CHECK(!scroll_for_row(2, 0, 10, 100, 10));
  CHECK(!scroll_for_row(1, 0, 10, 100, 10));
  CHECK(!scroll_for_row(10, 0, 10, 100, 10));
  CHECK(scroll_for_row(0, 0, 10, 100, 10) == -45.0f);
  CHECK(scroll_for_row(11, 0, 10, 100, 10) == 65.0f);
  CHECK(scroll_for_row(0, 0, 10, 100, 5) == 0.0f);
  CHECK(scroll_for_row(9, 0, 10, 95, 0) == 5.0f);
  CHECK(scroll_for_row(3, 20, 10, 30, 0) == 40.0f);
  CHECK(!scroll_for_row(0, 20, 10, 10, 20));
  CHECK(scroll_for_row(0, 20, 10, 5, 22) == 20.0f);
}

void test_drop_rows() {
  const auto tree = make_tree();
  const auto rows = flatten_visible_rows(tree, "", 0, {"root", "root/sub"});
  CHECK(rows.size() == 4);
  CHECK(drop_folder_index(rows, 0) == 0);
  CHECK(drop_folder_index(rows, 1) == 1);
  CHECK(drop_folder_index(rows, 2) == 1);
  CHECK(drop_folder_index(rows, 3) == std::string::npos);
  CHECK(last_row_under(rows, 0) == 2);
  CHECK(last_row_under(rows, 1) == 2);
  CHECK(last_row_under(rows, 2) == 2);
  CHECK(last_row_under(rows, 3) == 3);

  const std::vector<TreeRow> mixed = {{nullptr, 0, true, true},
                                      {nullptr, 1, true, true},
                                      {nullptr, 2, false, false},
                                      {nullptr, 1, false, false},
                                      {nullptr, 0, true, false}};
  CHECK(drop_folder_index(mixed, 3) == 0);
  CHECK(last_row_under(mixed, 0) == 3);
  CHECK(last_row_under(mixed, 4) == 4);
  const auto closed = flatten_visible_rows(tree, "", 0, {});
  CHECK(closed.size() == 2);
  CHECK(drop_folder_index(closed, 0) == 0);
  CHECK(last_row_under(closed, 0) == 0);
  CHECK(drop_folder_index(closed, 1) == std::string::npos);
}

} // namespace

int main() {
  test_reveal();
  test_scroll();
  test_drop_rows();
}
