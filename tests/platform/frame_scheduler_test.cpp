#include "platform/frame_scheduler.hpp"

#include "../test_check.hpp"
#include <chrono>
#include <iostream>

namespace {

using namespace std::chrono_literals;
using Scheduler = platform::FrameScheduler;

void test_recent_interaction_and_live_visualization_use_vsync() {
  const Scheduler::TimePoint start{};
  Scheduler scheduler(start);

  CHECK(scheduler.wait_before_frame(start, false, false) == 0ms);
  scheduler.note_frame_started(start + 500ms);
  CHECK(scheduler.wait_before_frame(start + 500ms, false, true) == 0ms);

  scheduler.note_interaction(start + 1s);
  scheduler.note_frame_started(start + 1s);
  CHECK(scheduler.wait_before_frame(start + 1499ms, false, false) == 0ms);
}

void test_idle_frames_are_limited_without_adding_vsync_delay() {
  const Scheduler::TimePoint start{};
  Scheduler scheduler(start);
  scheduler.note_frame_started(start + 500ms);

  CHECK(scheduler.wait_before_frame(start + 500ms, false, false) == 67ms);
  CHECK(scheduler.wait_before_frame(start + 517ms, false, false) == 50ms);
  CHECK(scheduler.wait_before_frame(start + 567ms, false, false) == 0ms);
}

void test_minimized_window_uses_slowest_rate() {
  const Scheduler::TimePoint start{};
  Scheduler scheduler(start);
  scheduler.note_frame_started(start);

  // Minimized takes precedence even if audio is still feeding the waveform.
  CHECK(scheduler.wait_before_frame(start, true, true) == 200ms);
  CHECK(scheduler.wait_before_frame(start + 125ms, true, true) == 75ms);
}

} // namespace

int main() {
  test_recent_interaction_and_live_visualization_use_vsync();
  test_idle_frames_are_limited_without_adding_vsync_delay();
  test_minimized_window_uses_slowest_rate();
  std::cout << "All frame scheduler tests passed\n";
  return 0;
}
