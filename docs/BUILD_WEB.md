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

The build copies the web bundle into `build-web/web_dist`. Serve it, e.g.:

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

## Current limitations

- Unlike the desktop build, folders are imported rather than referenced.
  "Import Folder into Workspace" copies the chosen tree into `/megatoy`, and
  edits never reach the original folder -- use Download to get them back out.
- Clearing site data removes the library. Download anything you want to keep.
- MIDI input needs a browser with Web MIDI (Chromium-based) and one-time
  permission, granted from Preferences.
