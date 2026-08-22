#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ui {

/**
 * Which operators an edit applies to, and which one leads.
 *
 * `primary` is the operator touched most recently -- the lead of the
 * selection, in the sense NSTableView means by `selectedRow` sitting next to
 * `selectedRowIndexes`. It is the source Copy reads from and the operator a
 * multi-operator edit measures its delta against. It is called primary
 * rather than "focus" because ImGui already uses focus for window and item
 * keyboard focus, and the two would be read for each other on sight.
 *
 * Invariants: primary is always a member of the selection, and is -1 exactly
 * when the selection is empty.
 *
 * Slots are display slots -- 0..3 is OP1..OP4 as the editor draws them,
 * never the register order the chip stores them in.
 */
struct OperatorSelection {
  uint8_t selected = 0;
  int primary = -1;

  bool empty() const { return selected == 0; }
  bool contains(int slot) const;
  int count() const;
  /// Selected slots in ascending order.
  std::vector<int> slots() const;

  void clear();

  /// A plain click, or an edit on an operator outside the selection: the
  /// selection collapses onto this one.
  void select_only(int slot);

  /// Shift and a click: add an unselected operator, remove a selected one.
  /// Removing the lead hands it to the lowest operator still selected.
  void toggle_extend(int slot);

  /// Shift and an edit: extend, but never remove. A shift-drag that took a
  /// selected operator back out of the selection would abandon it mid-drag.
  void extend(int slot);

  /// An edit on an operator already in the selection: keep the group
  /// together and only move the lead. Without this, selecting two operators
  /// and then dragging one of them would drop the other immediately, and
  /// editing several at once could never happen.
  void touch(int slot);
};

/// "OP1".."OP4".
std::string operator_slot_label(int slot);

/// "OP1, OP3", or an empty string when nothing is selected.
std::string operator_selection_label(const OperatorSelection &selection);

} // namespace ui
