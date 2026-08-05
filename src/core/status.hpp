#pragma once

#include <chrono>
#include <cstdint>
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

struct Entry {
  std::uint64_t id = 0;
  Severity severity = Severity::Info;
  std::string message;
  std::chrono::steady_clock::time_point posted_at{};
};

/// Post a notification. Also mirrored to stdout/stderr so terminals and the
/// browser console keep working.
void post(Severity severity, std::string message);

inline void info(std::string message) {
  post(Severity::Info, std::move(message));
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
