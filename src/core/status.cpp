#include "core/status.hpp"

#include <algorithm>
#include <deque>
#include <iostream>
#include <mutex>

namespace megatoy::status {

namespace {

// Bounds memory when errors stack up unattended; the UI shows far fewer.
constexpr std::size_t kMaxEntries = 100;

std::mutex g_mutex;
std::deque<Entry> g_entries;
std::uint64_t g_next_id = 1;

} // namespace

void post(Severity severity, std::string message, Action action) {
  // Mirror before queueing so the terminal / browser console sees messages
  // even if the UI never renders (early startup failures).
  if (severity == Severity::Warning || severity == Severity::Error) {
    std::cerr << message << std::endl;
  } else {
    std::cout << message << std::endl;
  }

  const std::lock_guard<std::mutex> lock(g_mutex);
  Entry entry;
  entry.id = g_next_id++;
  entry.severity = severity;
  entry.message = std::move(message);
  entry.posted_at = std::chrono::steady_clock::now();
  entry.action = std::move(action);
  g_entries.push_back(std::move(entry));
  while (g_entries.size() > kMaxEntries) {
    g_entries.pop_front();
  }
}

std::vector<Entry> entries() {
  const std::lock_guard<std::mutex> lock(g_mutex);
  return {g_entries.begin(), g_entries.end()};
}

void dismiss(std::uint64_t id) {
  const std::lock_guard<std::mutex> lock(g_mutex);
  std::erase_if(g_entries, [id](const Entry &entry) { return entry.id == id; });
}

void clear() {
  const std::lock_guard<std::mutex> lock(g_mutex);
  g_entries.clear();
}

} // namespace megatoy::status
