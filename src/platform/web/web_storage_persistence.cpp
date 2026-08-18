#include "platform/web/web_storage_persistence.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include <emscripten.h>

namespace platform::web {

// clang-format off
EM_JS(void, megatoy_request_storage_persist_js, (), {
  if (Module.__megatoyStoragePersistTimer) {
    clearTimeout(Module.__megatoyStoragePersistTimer);
  }
  Module.__megatoyStoragePersistTimer = setTimeout(function() {
    Module.__megatoyStoragePersistTimer = 0;
    FS.syncfs(false, function(err) {
      if (err) {
        console.error("megatoy: could not persist browser storage", err);
      }
    });
  }, 250);
});
// clang-format on

void request_storage_persist() { megatoy_request_storage_persist_js(); }

} // namespace platform::web

#endif
