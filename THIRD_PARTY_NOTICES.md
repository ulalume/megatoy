# Third-Party Notices

Megatoy bundles or links the following third-party components. Each component
remains under its original license; please consult upstream projects for the
complete license texts.

| Component | Project URL | License(s) | Notes | License File |
| --- | --- | --- | --- | --- |
| libvgm (Nuked OPN2 core only) | https://github.com/ValleyBell/libvgm | Per-file mix in upstream repo | Megatoy links libvgm solely to access the Nuked OPN2 YM2612 core by Nuke.YKT. That core is licensed under LGPL-2.1-or-later; other libvgm cores remain unused. | `licenses/nuked-opn2-LGPL-2.1.txt` (full text from https://github.com/nukeykt/Nuked-OPN2) |
| nlohmann/json | https://github.com/nlohmann/json | MIT License | Header-only JSON library. | `licenses/nlohmann-json-MIT.txt` |
| nativefiledialog-extended | https://github.com/btzy/nativefiledialog-extended | zlib License | File dialog helper. | `licenses/nativefiledialog-extended-zlib.txt` |
| GLFW | https://github.com/glfw/glfw | zlib/libpng License | Windowing/input library. | `licenses/glfw-zlib.txt` |
| Dear ImGui | https://github.com/ocornut/imgui | MIT License | Immediate-mode GUI toolkit. | `licenses/imgui-MIT.txt` |
| RtMidi | https://github.com/thestk/rtmidi | MIT License | MIDI I/O abstraction. | `licenses/rtmidi-MIT.txt` |
| stb | https://github.com/nothings/stb | MIT License or Public Domain | Single-header image loader (`stb_image`). | `licenses/stb-license.txt` |
| chord_detector | https://github.com/ulalume/chord_detector | MIT License | Music theory utilities. | `licenses/chord-detector-MIT.txt` |
| kissfft | https://github.com/mborgerding/kissfft | BSD 3-Clause | FFT implementation. | `licenses/kissfft-BSD-3-Clause.txt` |
| SQLiteCpp | https://github.com/SRombauts/SQLiteCpp | MIT License | C++ wrapper around SQLite. | `licenses/sqlitecpp-MIT.txt` |
| IconFontCppHeaders | https://github.com/juliettef/IconFontCppHeaders | zlib/libpng License | Pre-generated icon font headers. | `licenses/iconfontcppheaders-zlib.txt` |
| Font Awesome Free assets | https://fontawesome.com | Icons: CC BY 4.0 • Fonts: SIL OFL 1.1 • Code: MIT | Copied verbatim from `assets/fonts/LICENSE.txt`. | `licenses/font-awesome-license.txt` |
| Built-in presets | Bundled `.dmp` patches | CC0 1.0 Universal | Listed alongside the assets. | `licenses/presets-CC0.txt` |

System libraries such as OpenGL and libcurl are used under the terms
distributed with your operating system or SDK.

If you redistribute Megatoy binaries, please keep this notice file together
with the relevant upstream license texts (for example, those already included
under `assets/` and in the fetched dependencies).
