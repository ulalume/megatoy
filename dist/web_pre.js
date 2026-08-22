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

  // Reading a large library back out of IndexedDB takes many seconds, and
  // until it lands there is nothing on screen but a black canvas -- long
  // enough that the app looks broken and gets reloaded, which is exactly
  // when storage is most fragile. Plain DOM, because this runs before the
  // wasm module (and therefore any of megatoy's own UI) exists.
  function showLoadingOverlay() {
    try {
      var parent = document.body || document.documentElement;
      if (!parent) {
        return null;
      }
      var overlay = document.createElement("div");
      overlay.id = "megatoy-loading";
      overlay.style.cssText = [
        "position:fixed",
        "inset:0",
        "z-index:2147483647",
        "display:flex",
        "align-items:center",
        "justify-content:center",
        "background:#000",
        "color:#c8c8c8",
        "font:16px/1.4 system-ui,-apple-system,sans-serif",
        "letter-spacing:0.08em",
      ].join(";");
      overlay.textContent = "Loading...";
      parent.appendChild(overlay);
      return overlay;
    } catch (e) {
      // Cosmetic only: never let it stand between the user and their patches.
      console.error("megatoy: could not show the loading overlay", e);
      return null;
    }
  }

  function hideLoadingOverlay(overlay) {
    try {
      if (overlay && overlay.parentNode) {
        overlay.parentNode.removeChild(overlay);
      }
    } catch (e) {
      console.error("megatoy: could not hide the loading overlay", e);
    }
  }

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
    // Every write path requests an explicit debounced flush, and deleting a
    // whole folder awaits its own (see web_folder_delete.cpp). autoPersist is
    // deliberately off: it schedules a syncfs of its own that knows nothing
    // about those, so the two ran alongside each other -- which is where the
    // "N FS.syncfs operations in flight" warnings came from. The explicit
    // flush is the one to keep, because only it reports completion back to
    // the app, and that is what retires a pending deletion.
    FS.mount(IDBFS, {}, root);
  } catch (e) {
    Module.__megatoyStorageLoadFailed = true;
    console.error(
      "megatoy: persistent storage unavailable, this session will not be saved",
      e
    );
    return;
  }

  addRunDependency("megatoy-idbfs");
  var overlay = showLoadingOverlay();
  FS.syncfs(true, function (err) {
    // Both paths: a failed populate still hands control to the app, and the
    // app must not start up underneath a permanent black sheet.
    hideLoadingOverlay(overlay);
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
