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

struct SaveState {
  std::string text;
  double time = 0.0;
};

SaveState &last_save() {
  static SaveState state;
  return state;
}

} // namespace

bool load_web_layout_ini() {
  if (ImGui::GetCurrentContext() == nullptr) {
    return false;
  }
  remove_previous_layouts();

  auto stored = platform::web::read_local_storage(kLayoutKey);
  auto &saved = last_save();
  saved.text = stored.value_or(std::string());
  saved.time = 0.0;
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
  size_t size = 0;
  const char *data = ImGui::SaveIniSettingsToMemory(&size);
  if (data == nullptr || size == 0) {
    return;
  }
  auto &saved = last_save();
  const std::string current(data, size);
  const double now = ImGui::GetTime();
  const bool throttle_ok = saved.time == 0.0 || (now - saved.time) > 1.0;
  if (current != saved.text && throttle_ok) {
    if (platform::web::write_local_storage(kLayoutKey, current)) {
      saved.text = current;
    }
    saved.time = now;
  }
  // Do not touch io.WantSaveIniSettings; we are saving on content diff instead.
}

} // namespace ui

#endif
