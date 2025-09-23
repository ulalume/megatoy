# YM2612 Patch Editor

![Screenshot](https://raw.githubusercontent.com/ulalume/megatoy/main/docs/screenshot.png)

I’m developing a **YM2612 patch editor** while learning about the chip along the way.
This project is built on **libvgm**.
> ⚠️ This project is in a very early stage.

## Features

- Load patches from:
  - `.dmp` (DefleMask Preset Format)
  - `.mml` (ctrmml)
  - `.fui` (Furnace Instrument)
  - `.rym2612` (RYM2612 Iconic FM Synthesizer Preset)
  *(Software-specific macros/effects are ignored.)*

- Save patches in a custom format.
- Play notes using the keyboard — it’s surprisingly fun!

## To Do

- [ ] Export in ctrmml format
- [ ] Export in dmp format
- [ ] Support sn76489

## Patches

You can load patch files by placing `.dmp`, `.fui`, `.rym2612`, or `.mml` (ctrmml) files in `~/megatoys/patches/`.

Recommended sources:

- **DefleMask Legacy** (https://www.deflemask.com/get_legacy/) - instruments/Genesis
- **Furnace** (https://github.com/tildearrow/furnace) - instruments/OPN
- **Rym2612 ReFill** (https://www.inphonik.com/press/press-release-rym2612-refill/) - free pack of 180 patches
- **RymCast** (https://www.inphonik.com/products/rymcast-genesis-vgm-player/) - extract `.dmp` from VGM/VGZ files

## Build

```
cmake -B build
cmake --build build -j$(sysctl -n hw.ncpu)

# Release Build
cmake -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --config Release -j$(sysctl -n hw.ncpu)
```

## Thanks

- [libvgm](https://github.com/ValleyBell/libvgm/)
- [YM2612 registers reference](https://plutiedev.com/ym2612-registers)
- [Official manual](https://segaretro.org/images/e/ef/YM2612_manual.pdf)
