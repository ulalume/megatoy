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

## Current limitations

- Only the built-in preset library is available; importing user patches and metadata storage are disabled.
- Exports trigger browser downloads (DMP and MML) instead of writing to disk.
- WebMIDI input is not yet implemented; use the soft keyboard to audition patches.

Future work includes browser storage for user patches and WebMIDI integration.
