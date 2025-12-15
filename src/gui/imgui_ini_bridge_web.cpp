#if defined(MEGATOY_PLATFORM_WEB)

#include "gui/imgui_ini_bridge.hpp"

#include "platform/web/local_storage.hpp"
#include <imgui.h>
#include <optional>
#include <string>

namespace ui {

void sync_web_imgui_ini(bool &first_frame, bool &web_ini_loaded) {
  if (web_ini_loaded || ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  auto stored = platform::web::read_local_storage("megatoy_imgui_ini");
  if (stored.has_value() && !stored->empty()) {
    ImGui::LoadIniSettingsFromMemory(stored->c_str(), stored->size());
    first_frame = false;
  } else {
    first_frame = true;
  }
  web_ini_loaded = true;
}

void save_web_imgui_ini_if_needed(bool &web_ini_loaded) {
  if (!web_ini_loaded || ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  ImGuiIO &io = ImGui::GetIO();
  if (!io.WantSaveIniSettings) {
    return;
  }
  size_t size = 0;
  const char *data = ImGui::SaveIniSettingsToMemory(&size);
  if (data != nullptr && size > 0) {
    platform::web::write_local_storage("megatoy_imgui_ini",
                                       std::string(data, size));
  }
  io.WantSaveIniSettings = false;
}

} // namespace ui

#endif
