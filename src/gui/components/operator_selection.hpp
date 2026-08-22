#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ui {

/**
 * Which operators an edit applies to, and which one leads.
 *
 * `primary` is the one touched most recently: Copy's source, and what a
 * multi-operator edit measures its delta against. It is always a member of
 * the selection, and -1 exactly when the selection is empty. Named primary
 * because ImGui already uses focus for window and item keyboard focus.
 *
 * Slots are display slots: 0..3 is OP1..OP4 as drawn.
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

  /// A plain click, or an edit outside the selection: collapse onto this one.
  void select_only(int slot);

  /// Shift and a click: add if absent, remove if present. Removing the lead
  /// hands it to the lowest operator still selected.
  void toggle_extend(int slot);

  /// Shift and an edit: extend, never remove -- a shift-drag must not abandon
  /// the operator under the cursor.
  void extend(int slot);

  /// An edit inside the selection: keep the group, move only the lead.
  /// Without it, dragging one of two selected operators would drop the other.
  void touch(int slot);
};

/// "OP1".."OP4".
std::string operator_slot_label(int slot);

/// "OP1, OP3", or an empty string when nothing is selected.
std::string operator_selection_label(const OperatorSelection &selection);

} // namespace ui
