# Megatoy Linux Install Guide

This document covers how to build Megatoy on Linux (tested with Ubuntu/Debian) and enable realtime audio drivers.
If you build without PulseAudio/ALSA support you will only get the WaveWrite backend, which produces no live audio or waveform updates.

## Display Server Support

Megatoy uses GLFW which supports both **Wayland** and **X11** display servers. A single binary works on both systems:

- **Wayland systems**: GLFW automatically uses Wayland when available, falls back to XWayland if needed
- **X11 systems**: GLFW uses X11 directly
- **Mixed environments**: Works seamlessly without separate builds

## 1. Required packages

```bash
sudo apt update
sudo apt install build-essential pkg-config git \
    libwayland-dev libxkbcommon-dev xorg-dev \
    libasound2-dev libpulse-dev
```

**Package notes:**
- `libwayland-dev libxkbcommon-dev`: Enable native Wayland support
- `xorg-dev`: Meta-package providing all X11 development libraries (replaces individual X11 packages)
- `libasound2-dev libpulse-dev`: Mandatory for realtime audio (libvgm ALSA/PulseAudio drivers)

## 2. Install CMake 3.24+

Ubuntu 22.04 ships CMake 3.22, which is too old for some dependencies. Upgrade to 3.24 or newer:

```bash
# Check current version
cmake --version

# Install latest CMake via Kitware APT repository
wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | sudo tee /etc/apt/trusted.gpg.d/kitware.gpg >/dev/null
echo 'deb https://apt.kitware.com/ubuntu/ focal main' | sudo tee /etc/apt/sources.list.d/kitware.list >/dev/null
sudo apt update && sudo apt install cmake

# Verify version is 3.24+
cmake --version
```

Alternative method using pip:
```bash
python3 -m pip install --user --upgrade cmake
# Ensure ~/.local/bin appears before /usr/bin in your PATH
```

## 3. Fetch the source

```bash
git clone https://github.com/ulalume/megatoy.git
cd megatoy
```

## 4. Configure and build

Run CMake with ALSA/Pulse drivers explicitly enabled:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
    -DAUDIODRV_ALSA=ON -DAUDIODRV_PULSE=ON
cmake --build build-release --config Release --parallel
```

Keeping `AUDIODRV_ALSA`/`AUDIODRV_PULSE` ON prevents the build from falling back to WaveWrite-only.

## 5. Run and verify

```bash
./build-release/megatoy
```

**Expected output for working audio:**
```
Available audio drivers:
  0: PulseAudio
  1: ALSA
Using audio driver: PulseAudio
```

**Display server verification:**
```bash
# Check which display server is active
echo $XDG_SESSION_TYPE    # Shows 'wayland' or 'x11'

# Force specific display server (optional testing)
WAYLAND_DISPLAY=wayland-0 ./build-release/megatoy  # Force Wayland
DISPLAY=:0 ./build-release/megatoy                 # Force X11
```

## 6. Troubleshooting

### Audio Issues

- **`AudioDrv_Start failed: 241` / frozen waveform**
  
  Only the WaveWrite backend was built. Solution:
  ```bash
  # Reinstall audio dependencies
  sudo apt install libasound2-dev libpulse-dev
  # Clean and rebuild
  rm -rf build-release
  cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
      -DAUDIODRV_ALSA=ON -DAUDIODRV_PULSE=ON
  cmake --build build-release --config Release --parallel
  ```

- **PulseAudio unavailable on your system**
  
  Reconfigure with ALSA-only:
  ```bash
  cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
      -DAUDIODRV_ALSA=ON -DAUDIODRV_PULSE=OFF
  ```

### Display Issues

- **Application doesn't start on Wayland**
  
  Try XWayland compatibility mode:
  ```bash
  DISPLAY=:0 ./build-release/megatoy
  ```

- **Missing display libraries**
  
  Ensure all display dependencies are installed:
  ```bash
  sudo apt install libwayland-dev libxkbcommon-dev xorg-dev
  ```

### Build Verification

You can verify that audio drivers were properly built by checking:
```bash
grep -r "AUDDRV_ALSA\|AUDDRV_PULSE" build-release/_deps/libvgm-build/audio/CMakeFiles/vgm-audio.dir/flags.make
```
This should show `-D AUDDRV_ALSA` and/or `-D AUDDRV_PULSE` flags.

## Performance Notes

For distribution builds with generic x86-64 compatibility:
```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
    -DAUDIODRV_ALSA=ON -DAUDIODRV_PULSE=ON \
    -DMEGATOY_GENERAL_X86_64_LINUX=ON
```

With these steps Megatoy should produce audio and realtime visuals on both Wayland and X11 systems.