#if defined(MEGATOY_PLATFORM_WEB)

#include "platform/run_loop.hpp"

#include "platform/platform_config.hpp"
#include <emscripten.h>
#include <functional>

struct RuntimeContext;

namespace {

void web_loop_trampoline(void *arg) {
  auto *callback = static_cast<std::function<bool()> *>(arg);
  if (!callback) {
    emscripten_cancel_main_loop();
    return;
  }
  if (!(*callback)()) {
    emscripten_cancel_main_loop();
  }
}

} // namespace

namespace platform {

void run_main_loop(RuntimeContext &runtime,
                   const std::function<bool()> &frame_callback) {
  // emscripten keeps the callback pointer; keep it alive for the lifetime of
  // the main loop.
  static std::function<bool()> *callback_holder =
      new std::function<bool()>(frame_callback);
  emscripten_set_main_loop_arg(web_loop_trampoline, callback_holder, 0, true);
}

} // namespace platform

#endif
