#pragma once

#include "platform/platform_config.hpp"
#include "runtime_context.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include <emscripten.h>
#endif
#include <functional>

namespace platform {

using RunFrameFunc = bool (*)(RuntimeContext &);

inline void run_main_loop(RuntimeContext &runtime, RunFrameFunc frame_func) {
#if defined(MEGATOY_PLATFORM_WEB)
  struct CallbackData {
    RuntimeContext *runtime = nullptr;
    RunFrameFunc func = nullptr;
  };
  static CallbackData callback_data{};
  callback_data.runtime = &runtime;
  callback_data.func = frame_func;

  auto trampoline = [](void *arg) {
    auto *data = static_cast<CallbackData *>(arg);
    if (!data || !data->func) {
      emscripten_cancel_main_loop();
      return;
    }
    if (!data->func(*data->runtime)) {
      emscripten_cancel_main_loop();
    }
  };
  // simulate_infinite_loop leaves main() by throwing a JS "unwind"
  // exception. With wasm exceptions enabled that unwind runs C++
  // destructors on its way out, destroying the app the moment the loop is
  // registered. Register the loop and return normally instead; the caller
  // keeps the app alive in static storage and the runtime stays resident
  // (EXIT_RUNTIME=0).
  emscripten_set_main_loop_arg(trampoline, &callback_data, 0, false);
#else
  while (frame_func(runtime)) {
  }
#endif
}

} // namespace platform
