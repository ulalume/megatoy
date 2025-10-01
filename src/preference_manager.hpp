#pragma once

#include "preferences_data.hpp"
#include "system/data_paths.hpp"

#include <filesystem>
#include <memory>

class PreferenceStorage;

class PreferenceManager {
public:
  using UIPreferences = ::UIPreferences;

  PreferenceManager();
  ~PreferenceManager();

  // Directory configuration
  void set_data_directory(const std::filesystem::path &path);
  std::filesystem::path get_data_directory() const;

  // Retrieve subdirectories
  std::filesystem::path get_user_patches_directory() const;
  std::filesystem::path get_patches_directory() const;
  std::filesystem::path get_export_directory() const;
  std::filesystem::path get_config_directory() const;
  std::filesystem::path get_imgui_ini_file() const;
  std::filesystem::path get_builtin_presets_directory() const;

  void set_theme(ui::styles::ThemeId theme);
  ui::styles::ThemeId theme() const;

  const UIPreferences &ui_preferences() const;
  void set_ui_preferences(const UIPreferences &preferences);

  // Directory picker dialog
  bool select_data_directory();

  // Prepare native file dialog system; safe to call multiple times
  bool initialize_file_dialog();

  // Save and load preferences
  bool save_preferences();
  bool load_preferences();

  // Validate directories and create them if missing
  bool ensure_directories_exist();

  // Reset individual preference groups
  void reset_data_directory();
  void reset_ui_preferences();

  // Check initialization state
  bool is_initialized() const;

private:
  // Disable copy and assignment
  PreferenceManager(const PreferenceManager &) = delete;
  PreferenceManager &operator=(const PreferenceManager &) = delete;

  PreferenceData to_data() const;
  void apply_loaded_data(const PreferenceData &data);

  // Internal members
  megatoy::system::DataPathProvider data_paths_;
  bool directories_initialized;
  ui::styles::ThemeId theme_;
  UIPreferences ui_preferences_;
  std::unique_ptr<PreferenceStorage> storage_;

  // Fetch the default data directory
  std::filesystem::path get_default_data_directory() const;

  // Path to the preferences file
  std::filesystem::path get_preferences_file_path() const;
};
