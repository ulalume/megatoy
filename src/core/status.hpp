#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/**
 * The one place user-facing notifications go.
 *
 * Anything the user should see -- a failed import, a completed save, audio
 * refusing to start -- is post()ed here and shown as a toast by the UI.
 * Before this, such messages went to stderr, where nobody running a windowed
 * app (let alone the web build) ever saw them.
 *
 * A process-wide service rather than something threaded through AppServices,
 * deliberately: the call sites include MIDI driver threads, browser
 * callbacks and platform code, and having to plumb a reference everywhere is
 * exactly why fprintf spread in the first place. post() is thread-safe and
 * buffers, so messages posted before the UI exists (e.g. during storage
 * bootstrap) appear once the first frame renders.
 *
 * The audio callback must NOT post -- this takes a lock and allocates.
 * Failures there are developer diagnostics and stay on stderr.
 */
namespace megatoy::status {

enum class Severity {
  Info,
  Success,
  Warning,
  Error,
};

/**
 * An optional button on the toast. `perform` runs on the UI thread, so it can
 * open a dialog. The rest of the toast still dismisses on click, and a toast
 * carrying an action does not expire on its own.
 */
struct Action {
  std::string label;
  std::function<void()> perform;

  bool valid() const { return !label.empty() && perform != nullptr; }
};

struct Entry {
  std::uint64_t id = 0;
  Severity severity = Severity::Info;
  std::string message;
  std::chrono::steady_clock::time_point posted_at{};
  Action action;
};

/// Post a notification. Also mirrored to stdout/stderr so terminals and the
/// browser console keep working.
void post(Severity severity, std::string message, Action action = {});

inline void info(std::string message, Action action = {}) {
  post(Severity::Info, std::move(message), std::move(action));
}
inline void success(std::string message) {
  post(Severity::Success, std::move(message));
}
inline void warning(std::string message) {
  post(Severity::Warning, std::move(message));
}
inline void error(std::string message) {
  post(Severity::Error, std::move(message));
}

/// Entries not yet dismissed, oldest first. Bounded; old entries fall off.
std::vector<Entry> entries();

/// Remove one entry -- the user closed its toast, or it expired.
void dismiss(std::uint64_t id);

/// Drop everything. For tests.
void clear();

} // namespace megatoy::status
