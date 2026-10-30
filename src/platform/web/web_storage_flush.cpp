#include "platform/web/web_storage_flush.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include <cstdint>
#include <emscripten.h>
#include <memory>
#include <utility>

namespace platform::web {

EM_JS_DEPS(megatoy_storage_flush, "$stringToNewUTF8");

namespace {

struct ActiveFlush {
  /// Identity handed to JavaScript. Monotonic rather than the slot address,
  /// which a later flush could reuse and a stale callback then claim.
  void *token = nullptr;
  AwaitedFlushProgress progress;
  std::function<void(bool, std::string)> on_complete;
};

std::unique_ptr<ActiveFlush> g_active;
std::uintptr_t g_next_token = 1;
bool g_flush_finished = false;

void finish_active(bool ok, std::string error) {
  if (!g_active) {
    return;
  }
  // Clear the slot before the callback runs: it is on the UI thread and may
  // start the next operation.
  auto active = std::move(g_active);
  g_active.reset();
  g_flush_finished = true;
  if (active->on_complete) {
    active->on_complete(ok, std::move(error));
  }
}

} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE void
megatoy_awaited_flush_done(void *token, int ok, const char *error) {
  if (!g_active || g_active->token != token) {
    return;
  }
  finish_active(ok != 0, ok ? std::string{}
                            : std::string(error != nullptr
                                              ? error
                                              : "storage sync failed"));
}

// clang-format off
EM_JS(void, megatoy_awaited_flush_js, (void *token), {
  FS.syncfs(false, function(error) {
    var ptr = 0;
    if (error) ptr = stringToNewUTF8("" + error);
    Module["_megatoy_awaited_flush_done"](token, error ? 0 : 1, ptr);
    if (ptr) _free(ptr);
  });
});
// clang-format on

bool begin_awaited_flush(
    std::string title, std::string detail,
    std::function<void(bool ok, std::string error)> on_complete) {
  if (g_active) {
    return false;
  }

  auto active = std::make_unique<ActiveFlush>();
  active->token = reinterpret_cast<void *>(g_next_token++);
  active->progress.title = std::move(title);
  active->progress.detail = std::move(detail);
  active->on_complete = std::move(on_complete);

  auto *token = active->token;
  g_active = std::move(active);
  megatoy_awaited_flush_js(token);
  return true;
}

const AwaitedFlushProgress *awaited_flush_progress() {
  return g_active ? &g_active->progress : nullptr;
}

bool take_awaited_flush_finished() {
  return !g_active && std::exchange(g_flush_finished, false);
}

} // namespace platform::web

#endif
