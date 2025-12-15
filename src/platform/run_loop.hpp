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
  emscripten_set_main_loop_arg(trampoline, &callback_data, 0, true);
#else
  while (frame_func(runtime)) {
  }
#endif
}

} // namespace platform
