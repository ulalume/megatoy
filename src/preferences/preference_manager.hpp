#pragma once

#include "preferences_data.hpp"
#include "system/path_service.hpp"
#include "workspace/workspace.hpp"

#include <filesystem>
#include <memory>

class PreferenceStorage;

/**
 * Persisted application settings.
 *
 * The workspace folder list lives here because every change to it has to be
 * written back immediately -- a folder the user added should still be there
 * after a crash.
 */
class PreferenceManager {
public:
  using UIPreferences = ::UIPreferences;

  explicit PreferenceManager(megatoy::system::PathService &paths);
  ~PreferenceManager();

  // Workspace folders
  megatoy::workspace::Workspace &workspace() { return workspace_; }
  const megatoy::workspace::Workspace &workspace() const { return workspace_; }

  bool add_workspace_folder(const std::filesystem::path &path);
  bool remove_workspace_folder(const std::filesystem::path &path);
  bool reorder_workspace_folder(std::size_t from, std::size_t to);

  /// Show a folder picker and add whatever the user chooses.
  /// Returns true if a folder was actually added.
  bool prompt_add_workspace_folder();

  bool show_builtin_presets() const { return show_builtin_presets_; }
  void set_show_builtin_presets(bool show);

  /// Starting directory for the next save/export dialog.
  std::filesystem::path last_save_directory() const;
  void set_last_save_directory(const std::filesystem::path &path);

  std::filesystem::path get_imgui_ini_file() const;

  void set_theme(ui::styles::ThemeId theme);
  ui::styles::ThemeId theme() const;

  const UIPreferences &ui_preferences() const;
  void set_ui_preferences(const UIPreferences &preferences);

  // Prepare native file dialog system; safe to call multiple times
  bool initialize_file_dialog();

  bool save_preferences();
  bool load_preferences();

  bool ensure_directories_exist();

  void reset_ui_preferences();

  bool is_initialized() const;

private:
  PreferenceManager(const PreferenceManager &) = delete;
  PreferenceManager &operator=(const PreferenceManager &) = delete;

  PreferenceData to_data() const;
  void apply_loaded_data(const PreferenceData &data);

  megatoy::system::PathService &paths_;
  bool directories_initialized;
  ui::styles::ThemeId theme_;
  UIPreferences ui_preferences_;
  megatoy::workspace::Workspace workspace_;
  std::filesystem::path last_save_directory_;
  bool show_builtin_presets_ = true;
  std::unique_ptr<PreferenceStorage> storage_;

  std::filesystem::path get_preferences_file_path() const;
};
