#include "platform/web/web_storage_persistence.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include <emscripten.h>

namespace platform::web {

// clang-format off
EM_JS(bool, megatoy_storage_load_failed_js, (), {
  return Module.__megatoyStorageLoadFailed === true;
});

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

bool storage_load_failed() { return megatoy_storage_load_failed_js(); }

void request_storage_persist() {
  if (!storage_load_failed()) {
    megatoy_request_storage_persist_js();
  }
}

} // namespace platform::web

#endif
