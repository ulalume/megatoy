#include "../test_check.hpp"
#include "gui/components/operator_selection.hpp"

#include <iostream>

namespace {

using ui::OperatorSelection;

void test_a_fresh_selection_is_empty() {
  OperatorSelection selection;
  CHECK(selection.empty());
  CHECK(selection.primary == -1);
  CHECK(selection.count() == 0);
  CHECK(ui::operator_selection_label(selection).empty());
}

void test_a_plain_click_collapses_the_selection() {
  OperatorSelection selection;
  selection.select_only(0);
  selection.extend(1);
  CHECK(selection.count() == 2);

  selection.select_only(2);
  CHECK(selection.count() == 1);
  CHECK(selection.contains(2));
  CHECK(!selection.contains(0));
  CHECK(!selection.contains(1));
  CHECK(selection.primary == 2);
}

// The sequence from the feature's own description: edit OP1, shift-click
// OP2, then plain-click OP3.
void test_the_documented_walkthrough() {
  OperatorSelection selection;

  selection.touch(0);
  CHECK(selection.contains(0));
  CHECK(selection.primary == 0);

  selection.extend(1);
  CHECK(selection.contains(0));
  CHECK(selection.contains(1));
  CHECK(selection.primary == 1);

  selection.select_only(2);
  CHECK(selection.count() == 1);
  CHECK(selection.contains(2));
  CHECK(selection.primary == 2);
}

void test_editing_inside_the_selection_keeps_the_group() {
  OperatorSelection selection;
  selection.select_only(0);
  selection.extend(1);

  selection.touch(0);
  CHECK(selection.count() == 2);
  CHECK(selection.contains(0));
  CHECK(selection.contains(1));
  CHECK(selection.primary == 0);
}

void test_editing_outside_the_selection_resets_it() {
  OperatorSelection selection;
  selection.select_only(0);
  selection.extend(1);

  selection.touch(3);
  CHECK(selection.count() == 1);
  CHECK(selection.contains(3));
  CHECK(selection.primary == 3);
}

void test_shift_click_removes_an_already_selected_operator() {
  OperatorSelection selection;
  selection.select_only(0);
  selection.toggle_extend(1);
  selection.toggle_extend(2);
  CHECK(selection.count() == 3);

  selection.toggle_extend(1);
  CHECK(selection.count() == 2);
  CHECK(!selection.contains(1));
  // Removing a follower leaves the lead alone.
  CHECK(selection.primary == 2);
}

void test_removing_the_lead_hands_it_on() {
  OperatorSelection selection;
  selection.select_only(1);
  selection.toggle_extend(3);
  CHECK(selection.primary == 3);

  selection.toggle_extend(3);
  CHECK(selection.count() == 1);
  CHECK(selection.primary == 1);
}

void test_removing_the_last_operator_empties_the_selection() {
  OperatorSelection selection;
  selection.select_only(2);
  selection.toggle_extend(2);
  CHECK(selection.empty());
  CHECK(selection.primary == -1);
}

// Shift-dragging a slider on an operator that is already selected must not
// take it back out of the selection halfway through the drag.
void test_extend_never_removes() {
  OperatorSelection selection;
  selection.select_only(0);
  selection.extend(1);
  selection.extend(1);
  CHECK(selection.count() == 2);
  CHECK(selection.contains(1));
  CHECK(selection.primary == 1);
}

void test_out_of_range_slots_are_ignored() {
  OperatorSelection selection;
  selection.select_only(0);

  selection.select_only(4);
  selection.extend(-1);
  selection.toggle_extend(9);
  selection.touch(4);

  CHECK(selection.count() == 1);
  CHECK(selection.contains(0));
  CHECK(selection.primary == 0);
}

void test_labels_read_in_display_order() {
  CHECK(ui::operator_slot_label(0) == "OP1");
  CHECK(ui::operator_slot_label(3) == "OP4");

  OperatorSelection selection;
  selection.select_only(2);
  selection.extend(0);
  CHECK(ui::operator_selection_label(selection) == "OP1, OP3");
}

void test_clear_drops_the_lead() {
  OperatorSelection selection;
  selection.select_only(0);
  selection.extend(2);
  selection.clear();
  CHECK(selection.empty());
  CHECK(selection.primary == -1);
  CHECK(selection.slots().empty());
}

} // namespace

int main() {
  test_a_fresh_selection_is_empty();
  test_a_plain_click_collapses_the_selection();
  test_the_documented_walkthrough();
  test_editing_inside_the_selection_keeps_the_group();
  test_editing_outside_the_selection_resets_it();
  test_shift_click_removes_an_already_selected_operator();
  test_removing_the_lead_hands_it_on();
  test_removing_the_last_operator_empties_the_selection();
  test_extend_never_removes();
  test_out_of_range_slots_are_ignored();
  test_labels_read_in_display_order();
  test_clear_drops_the_lead();

  std::cout << "All operator selection tests passed\n";
  return 0;
}
