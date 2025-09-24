#pragma once

#include <filesystem>

class PreferenceManager {
public:
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

  // Directory picker dialog
  bool select_data_directory();

  // Prepare native file dialog system; safe to call multiple times
  bool initialize_file_dialog();

  // Save and load preferences
  bool save_preferences();
  bool load_preferences();

  // Validate directories and create them if missing
  bool ensure_directories_exist();

  // Reset to default settings
  void reset_to_defaults();

  // Check initialization state
  bool is_initialized() const;

private:
  // Disable copy and assignment
  PreferenceManager(const PreferenceManager &) = delete;
  PreferenceManager &operator=(const PreferenceManager &) = delete;

  // Internal members
  std::filesystem::path data_directory;
  bool directories_initialized;

  // Fetch the default data directory
  std::filesystem::path get_default_data_directory() const;

  // Path to the preferences file
  std::filesystem::path get_preferences_file_path() const;
};
