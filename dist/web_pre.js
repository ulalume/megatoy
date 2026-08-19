// Give the web build a real, persistent filesystem.
//
// Emscripten's default MEMFS lives in wasm memory and is gone on reload, so
// the web version used to keep the whole patch library in a single
// localStorage JSON blob. IDBFS is a normal Emscripten filesystem backed by
// IndexedDB: patches become actual files in actual directories, which lets
// the web build use the same workspace and storage code as the desktop one.
//
// The mount has to be populated from IndexedDB *before* main() runs, since
// startup reads the workspace immediately. addRunDependency holds main() back
// until the asynchronous load finishes.

Module["preRun"] = Module["preRun"] || [];
Module["preRun"].push(function () {
  var root = "/megatoy";

  // ERRNO_CODES is not exported in optimized builds, so avoid a symbolic
  // EEXIST check by testing the path before mkdir.
  if (!FS.analyzePath(root).exists) {
    try {
      FS.mkdir(root);
    } catch (e) {
      console.error("megatoy: could not create " + root, e);
      return;
    }
  }

  try {
    // autoPersist is the baseline for incidental writes. User-visible save,
    // import, and delete paths also request an explicit debounced flush so a
    // reload immediately after the operation cannot outrun persistence.
    FS.mount(IDBFS, { autoPersist: true }, root);
  } catch (e) {
    Module.__megatoyStorageLoadFailed = true;
    console.error(
      "megatoy: persistent storage unavailable, this session will not be saved",
      e
    );
    return;
  }

  addRunDependency("megatoy-idbfs");
  FS.syncfs(true, function (err) {
    if (err) {
      Module.__megatoyStorageLoadFailed = true;
      console.error("megatoy: could not load persistent storage", err);
      try {
        FS.unmount(root);
      } catch (e) {
        console.error("megatoy: could not detach persistent storage", e);
      }
    }
    removeRunDependency("megatoy-idbfs");
  });
});
