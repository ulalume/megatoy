#pragma once

#include "preferences_data.hpp"
#include "system/path_service.hpp"
#include "workspace/workspace.hpp"
#include <functional>

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
  /// Follow a workspace folder that was renamed on disk. `from` must be a
  /// path taken from workspace().folders().
  bool rename_workspace_folder(const std::filesystem::path &from,
                               const std::filesystem::path &to);
  bool workspace_folder_is_protected(const std::filesystem::path &path) const;
  bool reorder_workspace_folder(std::size_t from, std::size_t to);
  bool refresh_workspace_availability();

  /**
   * Bring a folder of patches into the workspace.
   *
   * On desktop this opens the native folder picker and the folder is read in
   * place. The browser cannot reference a folder on disk that way -- Firefox
   * and Safari ship no directory picker at all -- so there it imports a copy
   * into persistent storage instead, and because the picker and the reads are
   * asynchronous `on_changed` may run several frames later. It is not called
   * if the user cancels.
   */
  void request_add_workspace_folder(std::function<void()> on_changed);

  /// True when adding a folder copies it rather than referencing it.
  static bool folder_add_is_import();

  bool show_builtin_presets() const { return show_builtin_presets_; }
  void set_show_builtin_presets(bool show);

  /// Starting directory for the next save/export dialog.
  std::filesystem::path last_save_directory() const;
  void set_last_save_directory(const std::filesystem::path &path);
  const std::string &last_patch_path() const;
  void set_last_patch_path(const std::string &path);

  /// The version whose change log the user has already been offered.
  const std::string &last_seen_version() const;
  void set_last_seen_version(const std::string &version);

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
  std::string last_patch_path_;
  std::string last_seen_version_;
  bool show_builtin_presets_ = true;
  bool legacy_workspace_migration_complete_ = false;
  bool legacy_metadata_migration_complete_ = false;
  std::unique_ptr<PreferenceStorage> storage_;

  std::filesystem::path get_preferences_file_path() const;
  bool migrate_legacy_metadata(
      const std::filesystem::path &preferred_workspace = {});
};
