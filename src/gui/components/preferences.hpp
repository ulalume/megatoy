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
  bool &open_add_folder_dialog;
  const megatoy::system::DirectoryPaths &paths;
  const std::vector<std::string> &connected_midi_devices;
  std::string midi_status_message;
  bool show_web_midi_button = false;
  bool web_midi_button_disabled = false;
  std::function<void()> request_web_midi;
  /// Called after the workspace folder list changes.
  std::function<void()> sync_workspace;
  std::function<void(const std::filesystem::path &)> remove_workspace_folder;
  std::function<void(ui::styles::ThemeId theme_id)> apply_theme;
  /// Called with the new interface scale preference.
  std::function<void(float preference)> apply_ui_scale;
  bool allow_workspace_ui = true;
  /// Frames the audio device uses when the buffer preference is unset.
  int default_audio_buffer_frames = 0;
  /// Enlargement the display asks content for, for the automatic label.
  float display_scale = 1.0f;
};

void render_preferences_window(const char *title, PreferencesContext &context);

} // namespace ui
