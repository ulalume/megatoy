#pragma once

#include <cstddef>

namespace ui {

// Web build: load ImGui ini from persistent storage. No-op on native.
// layout_loaded is set true if settings were found.
void sync_web_imgui_ini(bool &first_frame, bool &web_ini_loaded,
                        bool &layout_loaded);

// Web build: save ImGui ini when contents change. No-op on native.
void save_web_imgui_ini_if_needed(bool &web_ini_loaded);

} // namespace ui
