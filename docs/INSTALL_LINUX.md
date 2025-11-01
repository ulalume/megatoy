# Megatoy Linux Install Guide

This document covers how to build Megatoy on Linux (tested with Ubuntu/Debian) and enable the realtime audio drivers.
If you build without PulseAudio/ALSA support you will only get the WaveWrite backend, which produces no live audio or waveform updates.

## 1. Required packages

```bash
sudo apt update
sudo apt install build-essential pkg-config git \
    libgl1-mesa-dev libx11-dev libxrandr-dev libxi-dev libxinerama-dev \
    libxcursor-dev libxext-dev libxfixes-dev libasound2-dev libpulse-dev
```

Megatoy relies on libvgm’s ALSA/PulseAudio drivers, so `libasound2-dev` and `libpulse-dev` are mandatory.

## 2. Install CMake 3.24+

Ubuntu 22.04 ships CMake 3.22, which is too old for some library. Upgrade to 3.24 or newer using any method you prefer (for example, the [Kitware APT repository](https://apt.kitware.com/)).

```bash
# Example: install the latest CMake via the Kitware APT repository
wget https://apt.kitware.com/kitware-archive.sh
sudo bash kitware-archive.sh   # script prompts for confirmation
sudo apt install cmake

cmake --version  # should report 3.24+
```

You can also install CMake locally with `python3 -m pip install --user --upgrade cmake`. Ensure `~/.local/bin` appears before `/usr/bin` in your `PATH`.

## 3. Fetch the source

```bash
git clone https://github.com/ulalume/megatoy.git
cd megatoy
```

## 4. Configure and build

Run CMake with ALSA/Pulse drivers explicitly enabled.
Keeping `AUDIODRV_ALSA`/`AUDIODRV_PULSE` ON prevents the build from falling back to WaveWrite-only.

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
    -DAUDIODRV_ALSA=ON -DAUDIODRV_PULSE=ON
cmake --build build-release --config Release --parallel
```

## 5. Run and verify

```bash
./build-release/megatoy
```

If the console shows something like this, the realtime audio path is active:

```
Available audio drivers:
  0: PulseAudio
  1: ALSA
Using audio driver: PulseAudio
```

If you only see WaveWrite, you are missing dependencies or CMake disabled `AUDIODRV_ALSA`/`AUDIODRV_PULSE`. Confirm that `build-release/_deps/libvgm-build/audio/CMakeFiles/vgm-audio.dir/flags.make` contains `-D AUDDRV_ALSA` or `-D AUDDRV_PULSE`.

## 6. Troubleshooting

- **`AudioDrv_Start failed: 241` / frozen waveform**
  Only the WaveWrite backend was built. Reinstall the dependencies above, re-run CMake with the ALSA/Pulse options, and rebuild.
- **PulseAudio unavailable on your system**
  Reconfigure with `-DAUDIODRV_PULSE=OFF -DAUDIODRV_ALSA=ON` to force ALSA-only output.

With these steps Megatoy should produce audio and realtime visuals on Linux.
