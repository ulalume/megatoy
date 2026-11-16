#pragma once

#include "platform/platform_config.hpp"
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

  using ConfirmReplace = std::function<void(const std::string &message,
                                            std::function<void()> proceed)>;
#if !defined(MEGATOY_PLATFORM_WEB)
  /**
   * Bring `chosen`, a folder on disk, into the workspace, read in place.
   *
   * A folder already inside one in the workspace is turned away. One holding
   * folders in the workspace takes their place, once `confirm` has had the
   * user agree to `message` and called `proceed`; without `confirm` it is
   * turned away too.
   */
  void add_chosen_workspace_folder(const std::filesystem::path &chosen,
                                   std::function<void()> on_changed,
                                   ConfirmReplace confirm);
#endif

  /// True when adding a folder copies it rather than referencing it.
  static bool folder_add_is_import();

  bool show_builtin_presets() const { return show_builtin_presets_; }
  void set_show_builtin_presets(bool show);

  /// Starting directory for the next save/export dialog.
  std::filesystem::path last_save_directory() const;
  void set_last_save_directory(const std::filesystem::path &path);
  const std::string &last_patch_path() const;
  void set_last_patch_path(const std::string &path);
  const std::vector<OpenPatchRecord> &open_patches() const;
  int active_open_patch() const;
  void set_open_patches(std::vector<OpenPatchRecord> patches, int active);

  /// The version whose change log the user has already been offered.
  const std::string &last_seen_version() const;
  void set_last_seen_version(const std::string &version);

  std::filesystem::path get_imgui_ini_file() const;

  void set_theme(ui::styles::ThemeId theme);
  ui::styles::ThemeId theme() const;

  const UIPreferences &ui_preferences() const;
  void set_ui_preferences(const UIPreferences &preferences);

  bool save_preferences();
  bool load_preferences();

  bool ensure_directories_exist();

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
  std::vector<OpenPatchRecord> open_patches_;
  int active_open_patch_ = -1;
  std::string last_seen_version_;
  bool show_builtin_presets_ = true;
  std::unique_ptr<PreferenceStorage> storage_;

  std::filesystem::path get_preferences_file_path() const;
};
