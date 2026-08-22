#include "gui/components/operator_selection.hpp"

namespace ui {
namespace {

constexpr uint8_t bit(int slot) { return static_cast<uint8_t>(1u << slot); }

bool in_range(int slot) { return slot >= 0 && slot < 4; }

} // namespace

bool OperatorSelection::contains(int slot) const {
  return in_range(slot) && (selected & bit(slot)) != 0;
}

int OperatorSelection::count() const {
  int total = 0;
  for (int slot = 0; slot < 4; ++slot) {
    if (contains(slot)) {
      ++total;
    }
  }
  return total;
}

std::vector<int> OperatorSelection::slots() const {
  std::vector<int> result;
  for (int slot = 0; slot < 4; ++slot) {
    if (contains(slot)) {
      result.push_back(slot);
    }
  }
  return result;
}

void OperatorSelection::clear() {
  selected = 0;
  primary = -1;
}

void OperatorSelection::select_only(int slot) {
  if (!in_range(slot)) {
    return;
  }
  selected = bit(slot);
  primary = slot;
}

void OperatorSelection::extend(int slot) {
  if (!in_range(slot)) {
    return;
  }
  selected = static_cast<uint8_t>(selected | bit(slot));
  primary = slot;
}

void OperatorSelection::toggle_extend(int slot) {
  if (!in_range(slot)) {
    return;
  }
  if (!contains(slot)) {
    extend(slot);
    return;
  }

  selected = static_cast<uint8_t>(selected & ~bit(slot));
  if (primary != slot) {
    return;
  }
  // The lead just left the selection; hand it to whatever is still there.
  primary = -1;
  for (int candidate = 0; candidate < 4; ++candidate) {
    if (contains(candidate)) {
      primary = candidate;
      break;
    }
  }
}

void OperatorSelection::touch(int slot) {
  if (!in_range(slot)) {
    return;
  }
  if (contains(slot)) {
    primary = slot;
    return;
  }
  select_only(slot);
}

std::string operator_slot_label(int slot) {
  return "OP" + std::to_string(slot + 1);
}

std::string operator_selection_label(const OperatorSelection &selection) {
  std::string label;
  for (int slot : selection.slots()) {
    if (!label.empty()) {
      label += ", ";
    }
    label += operator_slot_label(slot);
  }
  return label;
}

} // namespace ui
