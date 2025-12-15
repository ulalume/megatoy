#pragma once

#include <functional>

struct RuntimeContext;

namespace platform {

// Runs the main loop until the frame callback returns false.
// The callback should perform one frame of work and return whether to continue.
void run_main_loop(RuntimeContext &runtime,
                   const std::function<bool()> &frame_callback);

} // namespace platform
