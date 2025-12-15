#if !defined(MEGATOY_PLATFORM_WEB)

#include "platform/run_loop.hpp"

#include "platform/platform_config.hpp"
#include <functional>

struct RuntimeContext;

namespace platform {

void run_main_loop(RuntimeContext &runtime,
                   const std::function<bool()> &frame_callback) {
  while (frame_callback()) {
  }
}

} // namespace platform

#endif
