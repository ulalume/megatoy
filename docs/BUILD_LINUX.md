# megatoy Linux Install Guide

This document covers how to build megatoy on Linux (tested with Ubuntu/Debian) and enable realtime audio drivers.
If you build without PulseAudio/ALSA support you will only get the WaveWrite backend, which produces no live audio or waveform updates.

## Display Server Support

megatoy uses SDL3 which supports both **Wayland** and **X11** display servers through the same binary:

- **Wayland systems**: SDL3 selects the native Wayland backend when available and falls back to XWayland if needed
- **X11 systems**: SDL3 uses the X11 driver directly
- **Mixed environments**: No separate builds or launch flags are required

## 1. Required packages

```bash
sudo apt update
sudo apt install build-essential pkg-config git \
    libwayland-dev libxkbcommon-dev xorg-dev \
    libcurl4-openssl-dev \
    libasound2-dev libpulse-dev
```

**Package notes:**
- `libwayland-dev libxkbcommon-dev`: Enable native Wayland support
- `xorg-dev`: Meta-package providing all X11 development libraries (replaces individual X11 packages)
- `libcurl4-openssl-dev`: Required for update checking functionality
- `libasound2-dev libpulse-dev`: Provide ALSA/PulseAudio backends for SDL3 audio and RtMidi, so the built binary can output sound on most Linux systems

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

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --config Release --parallel
```

## 5. Run and verify

```bash
./build-release/megatoy
```

**Expected output for working audio:**
```
SDL will automatically choose your default audio device; no extra driver
selection is required.
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

- **No sound output**

  SDL3 plays audio through the OS default output. Make sure other
  applications can play audio, then run megatoy with
  `SDL_AUDIO_DEVICE_APPNAME=megatoy ./build-release/megatoy` to force a fresh
  SDL device selection. You can also choose a specific backend via
  `SDL_AUDIODRIVER=pulseaudio` (or `alsa`, `pipewire`, etc.).

### Display Issues

- **Application doesn't start on Wayland**

  Try XWayland compatibility mode:
  ```bash
  DISPLAY=:0 ./build-release/megatoy
  ```

- **Missing display libraries**

  Ensure all display dependencies are installed:
  ```bash
  sudo apt install libwayland-dev libxkbcommon-dev xorg-dev libcurl4-openssl-dev
  ```

## Performance Notes

For distribution builds with generic x86-64 compatibility:
```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
    -DMEGATOY_GENERAL_X86_64_LINUX=ON
```

With these steps megatoy should produce audio and realtime visuals on both Wayland and X11 systems.
