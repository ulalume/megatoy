#include "platform/platform_config.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include "gui/imgui_ini_bridge.hpp"

#include "platform/web/local_storage.hpp"
#include <imgui.h>
#include <string>

namespace ui {

namespace {

// Versioned so a stored layout from a build whose default panel set differed
// is discarded once, instead of pinning users to a layout that has no place
// for a window that now exists. Bump this whenever the default layout gains
// or loses a panel.
constexpr const char *kLayoutKey = "megatoy_imgui_ini_v3";
constexpr const char *kPreviousLayoutKeys[] = {"megatoy_imgui_ini",
                                               "megatoy_imgui_ini_v2"};

// Layouts saved before a panel existed on web have no node for it, so it
// would come back floating. Drop them so the default is rebuilt.
void remove_previous_layouts() {
  static bool removed = false;
  if (removed) {
    return;
  }
  removed = true;
  for (const char *previous : kPreviousLayoutKeys) {
    platform::web::remove_local_storage(previous);
  }
}

std::string &last_saved() {
  static std::string text;
  return text;
}

} // namespace

bool load_web_layout_ini() {
  if (ImGui::GetCurrentContext() == nullptr) {
    return false;
  }
  remove_previous_layouts();

  auto stored = platform::web::read_local_storage(kLayoutKey);
  last_saved() = stored.value_or(std::string());
  if (!stored.has_value() || stored->empty()) {
    return false;
  }
  ImGui::LoadIniSettingsFromMemory(stored->c_str(), stored->size());
  return true;
}

void save_web_layout_ini() {
  if (ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  ImGuiIO &io = ImGui::GetIO();
  if (!io.WantSaveIniSettings) {
    return;
  }
  io.WantSaveIniSettings = false;
  size_t size = 0;
  const char *data = ImGui::SaveIniSettingsToMemory(&size);
  if (data == nullptr || size == 0) {
    return;
  }
  std::string current(data, size);
  if (current != last_saved() &&
      platform::web::write_local_storage(kLayoutKey, current)) {
    last_saved() = std::move(current);
  }
}

} // namespace ui

#endif
