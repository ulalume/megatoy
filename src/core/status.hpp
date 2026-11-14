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
 * open a dialog. The rest of the toast still dismisses on click.
 */
struct Action {
  std::string label;
  std::function<void()> perform;

  bool valid() const { return !label.empty() && perform != nullptr; }
};

/// How long a toast stays on screen, not counting while it is pointed at.
namespace lasts {
/// A result, or a hint about what to do instead.
inline constexpr std::chrono::seconds kShort{6};
/// Something to read through, or to undo.
inline constexpr std::chrono::seconds kMedium{8};
/// An operation that failed.
inline constexpr std::chrono::seconds kLong{12};
/// A state that goes on, or a button that stays useful: until it is closed.
inline constexpr std::chrono::seconds kUntilClosed{0};
} // namespace lasts

struct Entry {
  std::uint64_t id = 0;
  Severity severity = Severity::Info;
  std::string message;
  std::chrono::steady_clock::time_point posted_at{};
  std::chrono::seconds lasts = lasts::kUntilClosed;
  Action action;
};

/// Post a notification, and return its id. Also mirrored to stdout/stderr so
/// terminals and the browser console keep working.
std::uint64_t post(Severity severity, std::string message,
                   std::chrono::seconds lasts, Action action = {});

inline void info(std::string message,
                 std::chrono::seconds lasts = lasts::kShort,
                 Action action = {}) {
  post(Severity::Info, std::move(message), lasts, std::move(action));
}
inline void success(std::string message,
                    std::chrono::seconds lasts = lasts::kShort) {
  post(Severity::Success, std::move(message), lasts);
}
inline void warning(std::string message,
                    std::chrono::seconds lasts = lasts::kShort) {
  post(Severity::Warning, std::move(message), lasts);
}
inline void error(std::string message,
                  std::chrono::seconds lasts = lasts::kLong) {
  post(Severity::Error, std::move(message), lasts);
}

/// Entries not yet dismissed, oldest first. Bounded; old entries fall off.
std::vector<Entry> entries();

/// Remove one entry -- the user closed its toast, or it expired.
void dismiss(std::uint64_t id);

/// Whether the entry is still to be shown.
bool shown(std::uint64_t id);

/// Drop everything. For tests.
void clear();

} // namespace megatoy::status
