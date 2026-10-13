#pragma once

#include <string_view>

namespace ui {

// Web build: load the arrangement stored for the layout into ImGui and report
// whether one was found. No-op returning false on native, where ImGui reads
// the layout's ini file itself.
bool load_web_layout_ini(std::string_view layout_name);

// Web build: store ImGui's arrangement for the layout when it changed, at most
// once a second unless `immediately`. No-op on native.
void save_web_layout_ini(std::string_view layout_name, bool immediately);

} // namespace ui
