# Megatoy - YM2612 Patch Editor

> ⚠️ **Early Development**: This project is in active development and features may be incomplete.

![Screenshot](https://raw.githubusercontent.com/ulalume/megatoy/main/docs/screenshot.png)

A comprehensive **YM2612/OPN2 patch editor** for the Sega Genesis/Mega Drive sound chip. Designed for musicians, sound designers, and chiptune enthusiasts who want to create and experiment with FM synthesis patches.

## Key Features

### Patch Management

- **Load patches** from multiple formats: `.dmp`, `.fui`, `.rym2612`, `.mml`
- **Save patches** in custom format with full metadata
- **Export patches** to DefleMask (`.dmp`) and ctrmml (`.mml`) formats
- **Drag & drop** support for easy file loading
- **Organize patches** with metadata (star ratings and categories) for quick retrieval and filtering

### Real-time Audio & MIDI

- **Live audio playback** with built-in YM2612 emulation
- **MIDI input support** with velocity sensitivity
- **Software keyboard** for testing patches without MIDI hardware
- **Computer keyboard typing** with selectable scale and key for intuitive playing without MIDI gear
- **Real-time parameter adjustment** with immediate audio feedback

### Advanced Editing

- **Visual envelope editor** with interactive ADSR curves
- **Undo/Redo history** for safe experimentation

### MML Integration

- **Export to MML** for use with ctrmml
- **Real-time code preview** shows MML as you edit
- **One-click copy** to clipboard

## Build Instructions

```bash
# Clone repository
git clone https://github.com/ulalume/megatoy.git
cd megatoy

# Debug build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --parallel

# Release build
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --config Release --parallel
```

## Patch Sources

Load patches by placing files in `~/megatoy/patches/` (created automatically) or drag-and-drop into the application.

Built-in patches:

- [17 built-in CC0 patches](https://github.com/ulalume/megatoy/tree/main/assets/presets) - Contributions welcome! See [Issue #15](https://github.com/ulalume/megatoy/issues/15)

Recommended external collections:

- [DefleMask Legacy](https://www.deflemask.com/get_legacy/) - instruments/Genesis
- [Furnace](https://github.com/tildearrow/furnace) - instruments/OPN
- [Rym2612 ReFill](https://www.inphonik.com/press/press-release-rym2612-refill/) - free pack of 180 patches

## Thanks

- [libvgm](https://github.com/ValleyBell/libvgm/)
- [YM2612 registers reference](https://plutiedev.com/ym2612-registers)
- [Official manual](https://segaretro.org/images/e/ef/YM2612_manual.pdf)
- [Emulating the YM2612](https://jsgroth.dev/blog/posts/emulating-ym2612-part-1/)
