# megatoy Linux Install Guide

This document covers how to build megatoy on Linux (tested with Ubuntu/Debian) and enable realtime audio drivers.
Without the ALSA/PulseAudio development packages, SDL builds without those
backends and falls back to its silent dummy driver: no audio, no waveform.

One binary covers both Wayland and X11: SDL3 picks the backend at runtime,
falling back to XWayland where it has to. No separate builds or launch flags.

## 1. Required packages

```bash
sudo apt update
sudo apt install build-essential pkg-config git \
    libwayland-dev libxkbcommon-dev xorg-dev \
    libgtk-3-dev \
    libcurl4-openssl-dev \
    libasound2-dev libpulse-dev
```

**Package notes:**
- `libwayland-dev libxkbcommon-dev`: Enable native Wayland support
- `xorg-dev`: Meta-package providing all X11 development libraries (replaces individual X11 packages)
- `libgtk-3-dev`: Required by Native File Dialog Extended for the folder/save dialogs
- `libcurl4-openssl-dev`: Required for update checking functionality
- `libasound2-dev libpulse-dev`: Provide ALSA/PulseAudio backends for SDL3 audio and RtMidi, so the built binary can output sound on most Linux systems

## 2. Install CMake 3.24+

`cmake --version`; skip this step if it is already 3.24 or newer. Ubuntu 22.04
ships 3.22, which is too old.

```bash
python3 -m pip install --user --upgrade cmake
# Ensure ~/.local/bin comes before /usr/bin in your PATH
```

Or from the [Kitware APT repository](https://apt.kitware.com/), replacing
`jammy` with your release codename:

```bash
wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc | gpg --dearmor - | sudo tee /etc/apt/trusted.gpg.d/kitware.gpg >/dev/null
echo 'deb https://apt.kitware.com/ubuntu/ jammy main' | sudo tee /etc/apt/sources.list.d/kitware.list >/dev/null
sudo apt update && sudo apt install cmake
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

SDL picks the default audio device and display server on its own; there is
nothing to select. To force one for testing:

```bash
SDL_AUDIODRIVER=pulseaudio ./build-release/megatoy  # or alsa, pipewire
DISPLAY=:0 ./build-release/megatoy                  # X11 instead of Wayland
```

## 6. Troubleshooting

- **No sound output** — megatoy plays through the OS default output, so check
  that another application can first. Then try `SDL_AUDIODRIVER=` from step 5,
  or `SDL_AUDIO_DEVICE_APPNAME=megatoy` to force a fresh device selection.
- **Doesn't start on Wayland** — try XWayland with `DISPLAY=:0`.
- **Missing display libraries** — install the packages from step 1.

## Performance Notes

For distribution builds with generic x86-64 compatibility:
```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
    -DMEGATOY_GENERAL_X86_64_LINUX=ON
```
