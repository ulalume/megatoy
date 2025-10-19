#include "history_manager.hpp"

#include "app_state.hpp"
#include "history_entry.hpp"
#include <imgui.h>
#include <utility>

namespace history {

HistoryManager::HistoryManager(std::size_t max_entries)
    : max_entries_(max_entries ? max_entries : 1), undo_stack_(), redo_stack_(),
      active_transaction_() {}

void HistoryManager::clear() {
  undo_stack_.clear();
  redo_stack_.clear();
  active_transaction_.reset();
}

void HistoryManager::reset() { clear(); }

bool HistoryManager::can_undo() const { return !undo_stack_.empty(); }

bool HistoryManager::can_redo() const { return !redo_stack_.empty(); }

std::string_view HistoryManager::undo_label() const {
  return can_undo() ? undo_stack_.back()->label() : std::string_view{};
}

std::string_view HistoryManager::redo_label() const {
  return can_redo() ? redo_stack_.back()->label() : std::string_view{};
}

void HistoryManager::undo(AppState &app_state) {
  if (!can_undo()) {
    return;
  }

  cancel_transaction();

  auto entry = std::move(undo_stack_.back());
  undo_stack_.pop_back();
  entry->undo(app_state);
  redo_stack_.push_back(std::move(entry));
}

void HistoryManager::redo(AppState &app_state) {
  if (!can_redo()) {
    return;
  }

  cancel_transaction();

  auto entry = std::move(redo_stack_.back());
  redo_stack_.pop_back();
  entry->redo(app_state);
  undo_stack_.push_back(std::move(entry));
}

void HistoryManager::handle_shortcuts(AppState &app_state) {
  ImGuiIO &io = ImGui::GetIO();

  if (io.WantTextInput || app_state.input_state().text_input_focused) {
    return;
  }

  const bool primary_modifier = io.KeyCtrl || io.KeySuper;
  if (!primary_modifier) {
    return;
  }

  const bool shift = io.KeyShift;

  if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
    if (shift) {
      if (can_redo()) {
        redo(app_state);
      }
    } else if (can_undo()) {
      undo(app_state);
    }
  } else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
    if (can_redo()) {
      redo(app_state);
    }
  }
}

void HistoryManager::begin_transaction(std::string label, std::string merge_key,
                                       EntryFactory factory) {
  cancel_transaction();
  active_transaction_.emplace(ActiveTransaction{
      .label = std::move(label),
      .merge_key = std::move(merge_key),
      .factory = std::move(factory),
  });
}

void HistoryManager::commit_transaction(AppState &app_state) {
  if (!active_transaction_) {
    return;
  }

  auto factory = std::move(active_transaction_->factory);
  active_transaction_.reset();
  if (!factory) {
    return;
  }

  auto entry = factory(app_state);
  if (!entry) {
    return;
  }

  push_entry(std::move(entry));
}

void HistoryManager::cancel_transaction() { active_transaction_.reset(); }

void HistoryManager::push_entry(std::unique_ptr<HistoryEntry> entry) {
  if (!undo_stack_.empty() && !entry->merge_key().empty()) {
    if (undo_stack_.back()->merge_key() == entry->merge_key()) {
      if (undo_stack_.back()->try_merge(*entry)) {
        redo_stack_.clear();
        return;
      }
    }
  }

  undo_stack_.push_back(std::move(entry));
  redo_stack_.clear();
  trim_to_max();
}

void HistoryManager::trim_to_max() {
  while (undo_stack_.size() > max_entries_) {
    undo_stack_.erase(undo_stack_.begin());
  }
}

} // namespace history
