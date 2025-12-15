#pragma once

#include <cstddef>

namespace ui {

// Web build: load ImGui ini from persistent storage. No-op on native.
void sync_web_imgui_ini(bool &first_frame, bool &web_ini_loaded);

// Web build: save ImGui ini when ImGui requests it. No-op on native.
void save_web_imgui_ini_if_needed(bool &web_ini_loaded);

} // namespace ui
