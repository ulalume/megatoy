#pragma once

#include <algorithm>
#include <chrono>

namespace platform {

/**
 * Chooses how long the native event loop may sleep before drawing again.
 *
 * VSync already paces interactive and animated frames, so those must not add
 * another delay. Static UI is refreshed periodically for cursor blinking,
 * toasts and background state changes, while SDL input wakes the wait early.
 */
class FrameScheduler {
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  static constexpr auto kInteractionHold = std::chrono::milliseconds(500);
  static constexpr auto kIdleInterval = std::chrono::milliseconds(67);
  static constexpr auto kMinimizedInterval = std::chrono::milliseconds(200);

  FrameScheduler() : FrameScheduler(Clock::now()) {}

  explicit FrameScheduler(TimePoint started)
      : last_interaction_(started), last_frame_started_(started) {}

  void note_interaction(TimePoint now) { last_interaction_ = now; }
  void note_frame_started(TimePoint now) { last_frame_started_ = now; }

  std::chrono::milliseconds wait_before_frame(TimePoint now, bool minimized,
                                              bool live_visualization) const {
    if (!minimized &&
        (live_visualization || now - last_interaction_ < kInteractionHold)) {
      // SDL_GL_SwapWindow supplies the 60 Hz pacing through VSync.
      return std::chrono::milliseconds(0);
    }

    const auto interval = minimized ? kMinimizedInterval : kIdleInterval;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::max(Clock::duration::zero(), now - last_frame_started_));
    return elapsed >= interval ? std::chrono::milliseconds(0)
                               : interval - elapsed;
  }

private:
  TimePoint last_interaction_;
  TimePoint last_frame_started_;
};

} // namespace platform
