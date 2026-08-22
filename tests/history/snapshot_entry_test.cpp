#include "../test_check.hpp"
#include "history/snapshot_entry.hpp"

#include <iostream>
#include <string>

// The undo stack drops a step whose before and after are identical. Correct
// on its own terms, and also how a mis-ordered commit vanishes without a
// trace -- which is what checkboxes used to do.

namespace {

// Never invoked; undo/redo would need an AppContext and none of this does.
history::SnapshotEntry<int>::ApplyFn ignore_apply() {
  return [](AppContext &, const int &) {};
}

void test_an_unchanged_value_produces_no_entry() {
  auto entry = history::make_snapshot_entry<int>("Total Level", "op0.tl", 7, 7,
                                                 ignore_apply());
  CHECK(entry == nullptr);
}

void test_a_changed_value_produces_an_entry() {
  auto entry = history::make_snapshot_entry<int>("Total Level", "op0.tl", 7, 9,
                                                 ignore_apply());
  CHECK(entry != nullptr);
  CHECK(entry->label() == "Total Level");
  CHECK(entry->merge_key() == "op0.tl");
}

// A drag reports many edits; they collapse into the one the user made.
void test_entries_sharing_a_merge_key_collapse() {
  auto first = history::make_snapshot_entry<int>("Total Level", "op0.tl", 7, 9,
                                                 ignore_apply());
  auto second = history::make_snapshot_entry<int>("Total Level", "op0.tl", 9,
                                                  20, ignore_apply());
  CHECK(first != nullptr);
  CHECK(second != nullptr);
  CHECK(first->try_merge(*second));
}

void test_entries_with_different_merge_keys_stay_apart() {
  auto first = history::make_snapshot_entry<int>("Total Level", "op0.tl", 7, 9,
                                                 ignore_apply());
  auto second = history::make_snapshot_entry<int>("Attack Rate", "op0.ar", 3, 4,
                                                  ignore_apply());
  CHECK(!first->try_merge(*second));
}

// Instant edits carry no merge key: toggling on then off must not collapse
// into one step that undoes to nothing.
void test_entries_without_a_merge_key_never_collapse() {
  auto first =
      history::make_snapshot_entry<int>("OP1 Enable", "", 0, 1, ignore_apply());
  auto second =
      history::make_snapshot_entry<int>("OP1 Enable", "", 1, 0, ignore_apply());
  CHECK(first != nullptr);
  CHECK(second != nullptr);
  CHECK(first->merge_key().empty());
  CHECK(!first->try_merge(*second));
}

} // namespace

int main() {
  test_an_unchanged_value_produces_no_entry();
  test_a_changed_value_produces_an_entry();
  test_entries_sharing_a_merge_key_collapse();
  test_entries_with_different_merge_keys_stay_apart();
  test_entries_without_a_merge_key_never_collapse();

  std::cout << "All snapshot entry tests passed\n";
  return 0;
}
