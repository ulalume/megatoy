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

Patches live in a real filesystem, backed by IndexedDB through Emscripten's
IDBFS and mounted at `/megatoy` by `dist/web_pre.js`. The web build therefore
uses the same workspace and patch storage code as the desktop build: real
files, real folders, every supported format, and per-file writes.

A writable folder, `/megatoy/My Patches`, is created on first run so a new
visitor can save immediately. Preferences and the window layout stay in
`localStorage`, which suits a few kilobytes of settings.

## Current limitations

- **Folders are imported, not referenced.** The desktop build reads a folder in
  place; the browser cannot. Firefox and Safari implement no directory picker
  at all, and where one exists (`showDirectoryPicker()`, Chromium only) every
  read is asynchronous, which does not fit megatoy's synchronous filesystem
  interface. "Import Folder" therefore copies the chosen tree into `/megatoy`
  via `<input webkitdirectory>`, which every browser supports. Edits are saved
  in the browser, never written back to the original folder -- use Export to
  get a patch back out.
- Clearing site data removes the library. Export anything you want to keep.
- Exports trigger browser downloads instead of writing to disk.
- WebMIDI input is not yet implemented; use the soft keyboard to audition
  patches.
