#include "platform/platform_config.hpp"

#if !defined(MEGATOY_PLATFORM_WEB)

#include "gui/imgui_ini_bridge.hpp"

namespace ui {

bool load_web_layout_ini(std::string_view) { return false; }

void save_web_layout_ini(std::string_view, bool) {}

} // namespace ui

#endif
