# Web build (Emscripten)

megatoy can run in the browser via WebAssembly using the Emscripten SDK.

## Prerequisites

1. Install the latest [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) and activate it:
   ```bash
   git clone https://github.com/emscripten-core/emsdk.git
   cd emsdk
   ./emsdk install latest
   ./emsdk activate latest
   source ./emsdk_env.sh
   ```
2. Ensure `emcmake`, `em++`, and `emar` are in your `PATH`.

## Configure and build

```bash
# If your emscripten installation is read-only (e.g., Homebrew), point caches to a writable location.
export EM_CACHE="$HOME/.emscripten_cache"
export PKG_CONFIG_LIBDIR="$EM_CACHE/sysroot/lib/pkgconfig"

emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web --target megatoy --parallel
```

The build copies the required web bundle into `build-web/web-dist`. Serve the files from that directory, e.g.:

```bash
python3 -m http.server 8080 --directory ./build-web/web_dist
```

Navigate to `http://localhost:8080/index.html` in a WebGL2-capable browser.

## Troubleshooting

### `Emscripten.cmake` or `emcc` not found after upgrading the SDK

```
include could not find requested file:
  /opt/homebrew/Cellar/emscripten/<old version>/libexec/cmake/Modules/Platform/Emscripten.cmake
```

CMake caches the toolchain by absolute path, so a build directory configured
against one Emscripten version stops working when the SDK is upgraded. Delete
it and configure again:

```bash
rm -rf build-web
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
```

## Storage

Patches are real files in an IDBFS filesystem backed by IndexedDB, mounted at
`/megatoy` by `dist/web_pre.js`, so the web build shares the desktop build's
workspace and patch storage code. `/megatoy/My Patches` is created on first
run. Preferences and the window layout live in `localStorage`.

## Current limitations

- **Folders are imported, not referenced.** No browser offers a directory
  picker megatoy can read in place: Firefox and Safari have none, and
  Chromium's `showDirectoryPicker()` is asynchronous. "Import Folder" copies
  the chosen tree into `/megatoy` instead, and edits never reach the original
  -- use Export to get a patch back out.
- Clearing site data removes the library. Export anything you want to keep.
- Exports download rather than write to disk.
- MIDI input needs a browser with Web MIDI (Chromium-based) and one-time
  permission, granted from Preferences.
