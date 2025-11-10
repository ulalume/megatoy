#pragma once

#include "gui/styles/theme.hpp"
#include "preferences/preference_manager.hpp"
#include "system/path_service.hpp"
#include <imgui.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ui {

struct PreferencesContext {
  PreferenceManager &preferences;
  PreferenceManager::UIPreferences &ui_prefs;
  bool &open_directory_dialog;
  const megatoy::system::DirectoryPaths &paths;
  const std::vector<std::string> &connected_midi_devices;
  std::string midi_status_message;
  bool show_web_midi_button = false;
  bool web_midi_button_disabled = false;
  std::function<void()> request_web_midi;
  std::function<void()> sync_patch_directories;
  std::function<void(ui::styles::ThemeId theme_id)> apply_theme;
};

void render_preferences_window(const char *title, PreferencesContext &context);

} // namespace ui
