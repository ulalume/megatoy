#include "platform/platform_config.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include "gui/imgui_ini_bridge.hpp"

#include "platform/web/local_storage.hpp"
#include <imgui.h>
#include <optional>
#include <string>

namespace ui {

namespace {

// Versioned so a stored layout from a build whose default panel set differed
// is discarded once, instead of pinning users to a layout that has no place
// for a window that now exists. Bump this whenever the default layout gains
// or loses a panel.
constexpr const char *kLayoutKey = "megatoy_imgui_ini_v2";
constexpr const char *kPreviousLayoutKey = "megatoy_imgui_ini";

} // namespace

void sync_web_imgui_ini(bool &first_frame, bool &web_ini_loaded,
                        bool &layout_loaded) {
  if (web_ini_loaded) {
    return;
  }
  if (ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  // Layouts saved before the waveform panel existed on web have no node for
  // it, so it would come back floating. Drop them and rebuild the default.
  platform::web::remove_local_storage(kPreviousLayoutKey);

  auto stored = platform::web::read_local_storage(kLayoutKey);
  if (stored.has_value() && !stored->empty()) {
    ImGui::LoadIniSettingsFromMemory(stored->c_str(), stored->size());
    first_frame = false;
    layout_loaded = true;
  } else {
    first_frame = true;
    layout_loaded = false;
  }
  web_ini_loaded = true;
}

void save_web_imgui_ini_if_needed(bool &web_ini_loaded) {
  if (!web_ini_loaded) {
    return;
  }
  if (ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  ImGuiIO &io = ImGui::GetIO();
  size_t size = 0;
  const char *data = ImGui::SaveIniSettingsToMemory(&size);
  static std::string last_saved;
  static double last_save_time = 0.0;
  if (data != nullptr && size > 0) {
    const std::string current(data, size);
    const double now = ImGui::GetTime();
    const bool first_save = last_save_time == 0.0;
    const bool throttle_ok = first_save || (now - last_save_time) > 1.0;
    if (current != last_saved && throttle_ok) {
      const bool ok = platform::web::write_local_storage(kLayoutKey, current);
      (void)ok;
      last_saved = current;
      last_save_time = now;
    }
  }
  // Do not touch io.WantSaveIniSettings; we are saving on content diff instead.
}

} // namespace ui

#endif
