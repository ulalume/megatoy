#pragma once

#include "history_entry.hpp"
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct AppContext;

namespace history {

class HistoryManager {
public:
  explicit HistoryManager(std::size_t max_entries = 256);

  void clear();
  void reset();

  bool can_undo() const;
  bool can_redo() const;

  std::string_view undo_label() const;
  std::string_view redo_label() const;

  void undo(AppContext &app_context);
  void redo(AppContext &app_context);

  /**
   * Handle keyboard shortcuts for undo/redo
   * Should be called after polling events but before UI rendering
   */
  void handle_shortcuts(AppContext &app_context);

  using EntryFactory =
      std::function<std::unique_ptr<HistoryEntry>(AppContext &)>;

  void begin_transaction(std::string label, std::string merge_key,
                         EntryFactory factory);
  void commit_transaction(AppContext &app_context);
  void cancel_transaction();

private:
  void push_entry(std::unique_ptr<HistoryEntry> entry);
  void trim_to_max();

  std::size_t max_entries_;
  std::vector<std::unique_ptr<HistoryEntry>> undo_stack_;
  std::vector<std::unique_ptr<HistoryEntry>> redo_stack_;

  struct ActiveTransaction {
    std::string label;
    std::string merge_key;
    EntryFactory factory;
  };

  std::optional<ActiveTransaction> active_transaction_;
};

} // namespace history
