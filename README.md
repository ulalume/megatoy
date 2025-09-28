# YM2612 Patch Editor

![Screenshot](https://raw.githubusercontent.com/ulalume/megatoy/main/docs/screenshot.png)

A **YM2612 patch editor** for learning and experimentation with the chip.
This project is built on **libvgm**.
> ⚠️ This project is in a very early stage.

## Features

- Load patches from:
  - `.dmp` DefleMask Preset Format
  - `.mml` ctrmml
  - `.fui` Furnace Instrument
  - `.rym2612` RYM2612 Iconic FM Synthesizer Preset
  - *Software-specific macros/effects are ignored.*
- Save patches in a custom format.
- Export patches to:
  - `.dmp`
  - `.mml`
- Support MIDI Input

## Patches

You can load patch files by placing `.dmp`, `.fui`, `.rym2612`, or `.mml` (ctrmml) files in `~/megatoy/patches/`.

Recommended sources:

- **DefleMask Legacy** (https://www.deflemask.com/get_legacy/) - instruments/Genesis
- **Furnace** (https://github.com/tildearrow/furnace) - instruments/OPN
- **Rym2612 ReFill** (https://www.inphonik.com/press/press-release-rym2612-refill/) - free pack of 180 patches
- **RymCast** (https://www.inphonik.com/products/rymcast-genesis-vgm-player/) - extract `.dmp` from VGM/VGZ files
  > **Note:** RymCast 1.0.6 DMP Export appears to reverse the values of Operators 2 and 3.

## Build

```
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(sysctl -n hw.ncpu)

# Release Build
cmake -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --config Release -j$(sysctl -n hw.ncpu)
```

## Thanks

- [libvgm](https://github.com/ValleyBell/libvgm/)
- [YM2612 registers reference](https://plutiedev.com/ym2612-registers)
- [Official manual](https://segaretro.org/images/e/ef/YM2612_manual.pdf)
