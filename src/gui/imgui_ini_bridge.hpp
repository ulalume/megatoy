#pragma once

namespace ui {

// Web build: load the saved arrangement into ImGui and report whether one
// was found. No-op returning false on native, where ImGui reads the ini file.
bool load_web_layout_ini();

// Web build: store ImGui's arrangement when it changed, at most once a second.
// No-op on native.
void save_web_layout_ini();

} // namespace ui
