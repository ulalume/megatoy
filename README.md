# Megatoy - YM2612 Patch Editor

> ⚠️ **Early Development**: This project is in active development and features may be incomplete.

<p align="center">
  <img width="240" height="240" src="https://raw.githubusercontent.com/ulalume/megatoy/main/dist/icon.png" alt="app icon">
</p>

![Screenshot](https://raw.githubusercontent.com/ulalume/megatoy/main/docs/screenshot.png)

A comprehensive **YM2612/OPN2 patch editor** for the Sega Genesis/Mega Drive sound chip. Designed for musicians, sound designers, and chiptune enthusiasts who want to create and experiment with FM synthesis patches.

## Patch Lab (Experimental)

![Patch Lab Screenshot](https://raw.githubusercontent.com/ulalume/megatoy/main/docs/patch_lab.png)

Access via **View ▸ Patch Lab**.
Changes apply directly to your active patch with full undo support.

- **Random** – Generates a new patch each time.
- **Mix** – Mixes two patches from your patches by randomly choosing parameters from Patch A and Patch B. You can pick both sources via combo boxes and reuse seeds.
- **Morph** – Interpolates between two patches using a 0–1 blend slider with smooth parameter transitions.
- **Mutate** – Applies subtle or extreme perturbations to the current patch. Adjust variation depth, probability, and algorithm-lock toggles to taste, or repeat with a fixed seed for reproducible tweaks.

## Key Features

### Patch Management

- **Load patches** from multiple formats: `.dmp`, `.fui`, `.rym2612`, `.mml`
- **Save patches** in custom format with full metadata
- **Export patches** to DefleMask (`.dmp`) and [ctrmml](https://github.com/superctr/ctrmml)/ [mmlgui](https://github.com/superctr/mmlgui) (`.mml`) formats
- **Organize patches** with metadata (star ratings and categories) for quick retrieval and filtering
- **Drag & drop** support for easy file loading

### Real-time Audio & MIDI

- **MIDI input support** with velocity sensitivity
- **Computer keyboard typing** with selectable scale and key for intuitive playing without MIDI gear
- **Real-time parameter adjustment** with immediate audio feedback

### Advanced Editing

- **Patch Lab (Experimental)** window for randomized patch design, blending existing sounds, and quick mutations
- **Visual envelope editor** with interactive ADSR curves
- **Undo/Redo history** for safe experimentation

### MML Integration

- **Export to MML** for use with [ctrmml](https://github.com/superctr/ctrmml)
- **Real-time code preview** shows MML as you edit
- **One-click copy** to clipboard

## Patch Sources

Load patches by placing files in `~/megatoy/patches/` (created automatically) or drag-and-drop into the application.

Built-in patches:

- [17 built-in CC0 patches](https://github.com/ulalume/megatoy/tree/main/assets/presets) - Contributions welcome! See [Issue #15](https://github.com/ulalume/megatoy/issues/15)

Recommended external collections:

- [DefleMask Legacy](https://www.deflemask.com/get_legacy/) - instruments/Genesis
- [Furnace](https://github.com/tildearrow/furnace) - instruments/OPN
- [Rym2612 ReFill](https://www.inphonik.com/press/press-release-rym2612-refill/) - free pack of 180 patches

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

For distribution-specific notes on dependencies and audio drivers, see the Linux guide at [docs/BUILD_LINUX.md](docs/BUILD_LINUX.md).

## Thanks

- [libvgm](https://github.com/ValleyBell/libvgm/)
- [YM2612 registers reference](https://plutiedev.com/ym2612-registers)
- [Official manual](https://segaretro.org/images/e/ef/YM2612_manual.pdf)
- [Emulating the YM2612](https://jsgroth.dev/blog/posts/emulating-ym2612-part-1/)

## License

Megatoy is released under the MIT License (see `LICENSES.md` or
`licenses/megatoy-MIT.txt`). Third-party components remain under their own
licenses; refer to `licenses/THIRD_PARTY_NOTICES.md` for the mapping—including
the Nuked OPN2 YM2612 core (LGPL-2.1-or-later) lifted from libvgm—and keep the
entire `licenses/` directory next to any distributed binaries so recipients can
review every upstream license text.
