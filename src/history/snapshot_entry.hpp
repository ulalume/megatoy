#pragma once

#include "history_entry.hpp"
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

class AppState;

namespace history {

template <typename Value> class SnapshotEntry : public HistoryEntry {
public:
  using ApplyFn = std::function<void(AppState &, const Value &)>;

  SnapshotEntry(std::string label, std::string merge_key, Value before,
                Value after, ApplyFn apply)
      : label_(std::move(label)), merge_key_(std::move(merge_key)),
        before_(std::move(before)), after_(std::move(after)),
        apply_(std::move(apply)) {}

  void undo(AppState &app_state) override { apply_(app_state, before_); }

  void redo(AppState &app_state) override { apply_(app_state, after_); }

  std::string_view label() const override { return label_; }
  std::string_view merge_key() const override { return merge_key_; }

  bool try_merge(const HistoryEntry &next) override {
    if (merge_key_.empty()) {
      return false;
    }
    const auto *other = dynamic_cast<const SnapshotEntry *>(&next);
    if (!other) {
      return false;
    }
    if (other->merge_key_ != merge_key_) {
      return false;
    }
    after_ = other->after_;
    label_ = other->label_;
    return true;
  }

private:
  std::string label_;
  std::string merge_key_;
  Value before_;
  Value after_;
  ApplyFn apply_;
};

template <typename Value>
std::unique_ptr<HistoryEntry>
make_snapshot_entry(std::string label, std::string merge_key, Value before,
                    Value after, typename SnapshotEntry<Value>::ApplyFn apply) {
  if (before == after) {
    return nullptr;
  }

  return std::make_unique<SnapshotEntry<Value>>(
      std::move(label), std::move(merge_key), std::move(before),
      std::move(after), std::move(apply));
}

} // namespace history
