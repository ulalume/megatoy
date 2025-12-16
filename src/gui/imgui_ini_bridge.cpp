#include "platform/platform_config.hpp"

#if !defined(MEGATOY_PLATFORM_WEB)

#include "gui/imgui_ini_bridge.hpp"

namespace ui {

void sync_web_imgui_ini(bool &, bool &, bool &) {}

void save_web_imgui_ini_if_needed(bool &) {}

} // namespace ui

#endif
