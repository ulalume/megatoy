# Third-Party Notices

Megatoy bundles or links the following third-party components. Each component
remains under its original license; please consult upstream projects for the
complete license texts.

| Component | Project URL | License(s) | Notes |
| --- | --- | --- | --- |
| libvgm | https://github.com/ValleyBell/libvgm | Mixed: GPL-2.0-or-later, LGPL-2.1-or-later, BSD-3-Clause, MIT | Megatoy uses the Nuked OPN2 YM2612 core (LGPL-2.1-or-later). See `emu/cores` in libvgm for per-file SPDX identifiers. |
| nlohmann/json | https://github.com/nlohmann/json | MIT License | Header-only JSON library. |
| nativefiledialog-extended | https://github.com/btzy/nativefiledialog-extended | zlib License | File dialog helper. |
| GLFW | https://github.com/glfw/glfw | zlib/libpng License | Windowing/input library. |
| Dear ImGui | https://github.com/ocornut/imgui | MIT License | Immediate-mode GUI toolkit. |
| RtMidi | https://github.com/thestk/rtmidi | MIT License | MIDI I/O abstraction. |
| stb | https://github.com/nothings/stb | MIT License or Public Domain | Single-header image loader (`stb_image`). |
| chord_detector | https://github.com/ulalume/chord_detector | MIT License | Music theory utilities. |
| kissfft | https://github.com/mborgerding/kissfft | BSD 3-Clause | FFT implementation. |
| SQLiteCpp | https://github.com/SRombauts/SQLiteCpp | MIT License | C++ wrapper around SQLite. |
| IconFontCppHeaders | https://github.com/juliettef/IconFontCppHeaders | zlib/libpng License | Pre-generated icon font headers. |
| Font Awesome Free assets | Fonticons, Inc. | Icons: CC BY 4.0 • Fonts: SIL OFL 1.1 • Code: MIT | See `assets/fonts/LICENSE.txt` for attribution and redistribution requirements. |
| Built-in presets | `assets/presets/*.dmp` | CC0 1.0 Universal | Listed in `assets/presets/LICENSE`. |

System libraries such as OpenGL and libcurl are used under the terms
distributed with your operating system or SDK.

If you redistribute Megatoy binaries, please keep this notice file together
with the relevant upstream license texts (for example, those already included
under `assets/` and in the fetched dependencies).
