# megatoy - YM2612 Patch Editor

> ⚠️ **Early Development**: This project is in active development and features may be incomplete.

<p align="center">
  <img width="240" height="240" src="https://raw.githubusercontent.com/ulalume/megatoy/main/dist/icon.png" alt="app icon">
</p>

![Screenshot](https://raw.githubusercontent.com/ulalume/megatoy/main/docs/screenshot.png)

🕹️ **Try the Web Demo:** https://ulalume.github.io/megatoy/index.html

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

- **Load patches** from many formats, via [ym2612_format](https://github.com/ulalume/ym2612_format): DefleMask preset (`.dmp`) and module (`.dmf`), Furnace instrument (`.fui`) and module (`.fur`), [ctrmml](https://github.com/superctr/ctrmml)/ [mmlgui](https://github.com/superctr/mmlgui) (`.mml`), VOPM/MiOPMdrv (`.opm`), TFM Music Maker (`.tfi`), `.rym2612`, VGM Music Maker (`.vgi`), Echo (`.eif`), VGM/VGZ register log (`.vgm`/`.vgz`)
- **Save patches as** `.gin`, `.dmp`, `.fui`, `.eif`, `.tfi`, `.vgi`, or `.mml`
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

- **Real-time code preview** shows MML as you edit
- **One-click copy** to clipboard

## Patch Sources

You add folders of patches and it reads them where they are.

- **File ▸ Add Folder to Workspace...** — add as many folders as you like.
- **Drag & drop** a patch file onto the window to load it without adding a folder.
- Star ratings and categories are stored in a `.megatoy/patches.json` sidecar inside each folder.

Built-in patches:

- [built-in CC0 patches](https://github.com/ulalume/ym2612-patches)

Recommended external collections:

- [DefleMask Legacy](https://www.deflemask.com/get_legacy/) - instruments/Genesis
- [Furnace](https://github.com/tildearrow/furnace) - instruments/OPN
- [Rym2612 ReFill](https://www.inphonik.com/press/press-release-rym2612-refill/) - free pack of 180 patches

## Build Instructions

```bash
# Clone repository (CMake fetches the built-in preset patches at configure time)
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

For experimental WebAssembly builds, see [docs/BUILD_WEB.md](docs/BUILD_WEB.md).

## Thanks

- [ymfm](https://github.com/aaronsgiles/ymfm) - YM2612 emulation core
- [libvgm](https://github.com/ValleyBell/libvgm/) - sample rate conversion
- [YM2612 registers reference](https://plutiedev.com/ym2612-registers)
- [Official manual](https://segaretro.org/images/e/ef/YM2612_manual.pdf)
- [Emulating the YM2612](https://jsgroth.dev/blog/posts/emulating-ym2612-part-1/)

## License

MIT

Third party licenses: [licenses/THIRD_PARTY_NOTICES.md](licenses/THIRD_PARTY_NOTICES.md)
