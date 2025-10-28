#pragma once

#include <string_view>

struct AppContext;

namespace history {

class HistoryEntry {
public:
  virtual ~HistoryEntry() = default;

  virtual void undo(AppContext &app_context) = 0;
  virtual void redo(AppContext &app_context) = 0;

  virtual std::string_view label() const = 0;
  virtual std::string_view merge_key() const = 0;

  virtual bool try_merge(const HistoryEntry &next) { return false; }
};

} // namespace history
