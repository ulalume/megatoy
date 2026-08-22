#include "platform/web/web_storage_persistence.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include "core/status.hpp"
#include "platform/web/web_folder_delete.hpp"
#include <emscripten.h>

namespace platform::web {

EM_JS_DEPS(megatoy_storage_persist, "$stringToNewUTF8");

extern "C" EMSCRIPTEN_KEEPALIVE void
megatoy_storage_persist_failed(const char *error) {
  megatoy::status::error(
      "Could not persist browser storage: " +
      std::string(error != nullptr ? error : "unknown storage error"));
}

/// A committed flush is the only proof a deletion reached IndexedDB, so it is
/// also the only thing allowed to retire a deletion tombstone.
extern "C" EMSCRIPTEN_KEEPALIVE void megatoy_storage_persist_succeeded() {
  on_persist_succeeded();
}

// clang-format off
EM_JS(bool, megatoy_storage_load_failed_js, (), {
  return Module.__megatoyStorageLoadFailed === true;
});

// The debounce timer only ever guarded the *scheduling*. Once a flush was
// under way a new request started a second one alongside it -- Emscripten
// warns about that, and on a large library the two walked a tree that the
// other was still moving, which is where the ENOENT came from. A request
// during a flush is remembered and run after it instead.
EM_JS(void, megatoy_request_storage_persist_js, (), {
  var run = function() {
    if (Module.__megatoyStorageSyncing) {
      Module.__megatoyStorageDirty = true;
      return;
    }
    Module.__megatoyStorageSyncing = true;
    FS.syncfs(false, function(err) {
      Module.__megatoyStorageSyncing = false;
      var pending = Module.__megatoyStorageDirty === true;
      Module.__megatoyStorageDirty = false;
      if (err) {
        console.error("megatoy: could not persist browser storage", err);
        var ptr = stringToNewUTF8("" + err);
        Module["_megatoy_storage_persist_failed"](ptr);
        _free(ptr);
      } else {
        Module["_megatoy_storage_persist_succeeded"]();
      }
      // One retry per request that arrived mid-flush, so this cannot spin.
      if (pending) {
        run();
      }
    });
  };

  if (Module.__megatoyStoragePersistTimer) {
    clearTimeout(Module.__megatoyStoragePersistTimer);
  }
  Module.__megatoyStoragePersistTimer = setTimeout(function() {
    Module.__megatoyStoragePersistTimer = 0;
    run();
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
