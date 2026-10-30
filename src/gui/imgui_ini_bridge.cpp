#include "platform/platform_config.hpp"

#if !defined(MEGATOY_PLATFORM_WEB)

#include "gui/imgui_ini_bridge.hpp"

namespace ui {

bool load_web_layout_ini() { return false; }

void save_web_layout_ini() {}

} // namespace ui

#endif
