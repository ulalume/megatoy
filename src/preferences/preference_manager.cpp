#include "preference_manager.hpp"

#include "core/status.hpp"

#include "platform/file_dialog.hpp"
#include "platform/platform_config.hpp"
#include "workspace/path_policy.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include "platform/web/web_folder_import.hpp"
#include "platform/web/web_storage_bootstrap.hpp"
#else
#include "patches/legacy_metadata_migration.hpp"
#endif
#include "preference_storage.hpp"
#include <cstdlib>
#include <iostream>
#include <system_error>

PreferenceManager::PreferenceManager(megatoy::system::PathService &paths)
    : paths_(paths), directories_initialized(false),
      theme_(ui::styles::ThemeId::MegatoyDark),
      storage_(make_json_preference_storage(get_preferences_file_path(),
                                            paths_.file_system())) {
  ensure_directories_exist();
  load_preferences();

#if defined(MEGATOY_PLATFORM_WEB)
  // The browser has no folder for the user to point at, so the workspace is
  // seeded with a writable one inside persistent storage.
  if (platform::web::bootstrap_workspace(
          workspace_, megatoy::system::PathService::web_storage_root())) {
    save_preferences();
  }
#endif
}

PreferenceManager::~PreferenceManager() { platform::file_dialog::shutdown(); }

bool PreferenceManager::add_workspace_folder(
    const std::filesystem::path &path) {
  if (!workspace_.add(path)) {
    return false;
  }
  migrate_legacy_metadata();
  save_preferences();
  return true;
}

bool PreferenceManager::remove_workspace_folder(
    const std::filesystem::path &path) {
  if (workspace_folder_is_protected(path)) {
    megatoy::status::error("\"My Patches\" cannot be removed.");
    return false;
  }
  if (!workspace_.remove(path)) {
    return false;
  }
  save_preferences();
  return true;
}

bool PreferenceManager::workspace_folder_is_protected(
    const std::filesystem::path &path) const {
#if defined(MEGATOY_PLATFORM_WEB)
  return megatoy::workspace::paths_equal(
      path, platform::web::default_workspace_folder());
#else
  (void)path;
  return false;
#endif
}

bool PreferenceManager::reorder_workspace_folder(std::size_t from,
                                                 std::size_t to) {
  if (!workspace_.reorder(from, to)) {
    return false;
  }
  save_preferences();
  return true;
}

bool PreferenceManager::refresh_workspace_availability() {
  return workspace_.refresh();
}

bool PreferenceManager::folder_add_is_import() {
#if defined(MEGATOY_PLATFORM_WEB)
  return true;
#else
  return false;
#endif
}

void PreferenceManager::request_add_workspace_folder(
    std::function<void()> on_changed) {
#if defined(MEGATOY_PLATFORM_WEB)
  platform::web::import_folder(
      megatoy::system::PathService::web_storage_root(),
      [this, on_changed](platform::web::FolderImportResult result) {
        if (!result.ok) {
          megatoy::status::error("Folder import failed: " + result.error);
          return;
        }
        if (add_workspace_folder(result.path)) {
          megatoy::status::success("Imported \"" + result.folder_name + "\" (" +
                                   std::to_string(result.file_count) +
                                   " files)");
          if (on_changed) {
            on_changed();
          }
        } else {
          megatoy::status::warning("\"" + result.folder_name +
                                   "\" is already in the workspace.");
        }
      });
#else
  std::filesystem::path chosen;
  const auto start = last_save_directory();
  if (platform::file_dialog::pick_folder(start, chosen) !=
      platform::file_dialog::DialogResult::Ok) {
    return;
  }
  if (add_workspace_folder(chosen) && on_changed) {
    on_changed();
  }
#endif
}

void PreferenceManager::set_show_builtin_presets(bool show) {
  if (show == show_builtin_presets_) {
    return;
  }
  show_builtin_presets_ = show;
  save_preferences();
}

std::filesystem::path PreferenceManager::last_save_directory() const {
  if (!last_save_directory_.empty()) {
    std::error_code ec;
    if (std::filesystem::is_directory(last_save_directory_, ec) && !ec) {
      return last_save_directory_;
    }
  }
  if (auto folder = workspace_.default_save_folder()) {
    return *folder;
  }
  return megatoy::system::PathService::default_documents_directory();
}

void PreferenceManager::set_last_save_directory(
    const std::filesystem::path &path) {
  if (path.empty() || path == last_save_directory_) {
    return;
  }
  last_save_directory_ = path;
  save_preferences();
}

bool PreferenceManager::ensure_directories_exist() {
  directories_initialized = paths_.ensure_directories();
  return directories_initialized;
}

std::filesystem::path PreferenceManager::get_preferences_file_path() const {
  return paths_.paths().preferences_file;
}

std::filesystem::path PreferenceManager::get_imgui_ini_file() const {
  return paths_.paths().imgui_ini_file;
}

bool PreferenceManager::save_preferences() {
  if (!storage_) {
    return false;
  }
  return storage_->save(to_data());
}

bool PreferenceManager::load_preferences() {
  if (!storage_) {
    return false;
  }

  PreferenceData data = to_data();
  if (!storage_->load(data)) {
    return false;
  }

#if !defined(MEGATOY_PLATFORM_WEB)
  if (!data.legacy_workspace_migration_complete) {
    // Old releases created this tree without necessarily writing preferences.
    // The first workspace release could also save workspace_folders: [] after
    // discarding data_directory, so only our dedicated marker proves that the
    // one-time compatibility check has already run.
    if (data.workspace_folders.empty()) {
      const auto legacy =
          megatoy::system::PathService::legacy_default_patches_directory();
      std::error_code error;
      if (paths_.file_system().is_directory(legacy, error)) {
        data.workspace_folders = {legacy};
      } else if (error) {
        std::cerr << "Legacy workspace migration will be retried: "
                  << error.message() << "\n";
        apply_loaded_data(data);
        return true;
      }
    }
    data.legacy_workspace_migration_complete = true;
    data.migrated_legacy_workspace = true;
  }
#endif

  apply_loaded_data(data);
  const bool metadata_migrated =
      migrate_legacy_metadata(data.legacy_metadata_workspace);
  if (data.migrated_legacy_workspace || metadata_migrated) {
    // Rewrite immediately so the migration is one-shot.
    return save_preferences();
  }
  return true;
}

void PreferenceManager::reset_ui_preferences() {
  const auto current = ui_preferences_;
  ui_preferences_ = {};
  ui_preferences_.use_velocity = current.use_velocity;
  ui_preferences_.velocity_sensitivity_depth =
      current.velocity_sensitivity_depth;
  ui_preferences_.steal_oldest_note_when_full =
      current.steal_oldest_note_when_full;
  ui_preferences_.midi_keyboard_layout = current.midi_keyboard_layout;
  ui_preferences_.custom_typing_layout_keys = current.custom_typing_layout_keys;
  ui_preferences_.custom_typing_octave_down_key =
      current.custom_typing_octave_down_key;
  ui_preferences_.custom_typing_octave_up_key =
      current.custom_typing_octave_up_key;
  save_preferences();
}

bool PreferenceManager::is_initialized() const {
  return directories_initialized;
}

bool PreferenceManager::initialize_file_dialog() {
  return platform::file_dialog::initialize();
}

void PreferenceManager::set_theme(ui::styles::ThemeId theme) {
  if (theme_ == theme) {
    return;
  }
  theme_ = theme;
  save_preferences();
}

ui::styles::ThemeId PreferenceManager::theme() const { return theme_; }

const PreferenceManager::UIPreferences &
PreferenceManager::ui_preferences() const {
  return ui_preferences_;
}

void PreferenceManager::set_ui_preferences(
    const PreferenceManager::UIPreferences &preferences) {
  if (ui_preferences_ == preferences) {
    return;
  }

  ui_preferences_ = preferences;
  save_preferences();
}

PreferenceData PreferenceManager::to_data() const {
  PreferenceData data;
  data.workspace_folders = workspace_.paths();
  data.last_save_directory = last_save_directory_;
  data.show_builtin_presets = show_builtin_presets_;
  data.legacy_workspace_migration_complete =
      legacy_workspace_migration_complete_;
  data.legacy_metadata_migration_complete = legacy_metadata_migration_complete_;
  data.theme = theme_;
  data.ui_preferences = ui_preferences_;
  return data;
}

void PreferenceManager::apply_loaded_data(const PreferenceData &data) {
  workspace_.set_paths(data.workspace_folders);
  last_save_directory_ = data.last_save_directory;
  show_builtin_presets_ = data.show_builtin_presets;
  legacy_workspace_migration_complete_ =
      data.legacy_workspace_migration_complete;
  legacy_metadata_migration_complete_ = data.legacy_metadata_migration_complete;
  theme_ = data.theme;
  ui_preferences_ = data.ui_preferences;
}

bool PreferenceManager::migrate_legacy_metadata(
    const std::filesystem::path &preferred_workspace) {
#if defined(MEGATOY_PLATFORM_WEB)
  (void)preferred_workspace;
  return false;
#else
  if (legacy_metadata_migration_complete_) {
    return false;
  }
  const auto result = patches::migrate_legacy_metadata(
      paths_.paths().legacy_patch_metadata_db, workspace_, preferred_workspace);
  if (!result.complete) {
    if (!result.error.empty()) {
      std::cerr << "Legacy patch metadata migration will be retried: "
                << result.error << "\n";
    }
    return false;
  }
  legacy_metadata_migration_complete_ = true;
  return true;
#endif
}
