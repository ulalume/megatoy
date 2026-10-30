#include "preference_manager.hpp"

#include "core/status.hpp"

#include "platform/file_dialog.hpp"
#include "platform/platform_config.hpp"
#include "workspace/path_policy.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include "platform/web/web_folder_import.hpp"
#include "platform/web/web_storage_bootstrap.hpp"
#else
#include "patches/background_folder_scan.hpp"
#endif
#include "preference_storage.hpp"
#include <cstdlib>
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

bool PreferenceManager::rename_workspace_folder(const std::filesystem::path &from,
                                                const std::filesystem::path &to) {
  if (workspace_folder_is_protected(from)) {
    return false;
  }
  if (!workspace_.rename(from, to)) {
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
    std::function<void()> on_changed, ConfirmReplace confirm) {
#if defined(MEGATOY_PLATFORM_WEB)
  platform::web::import_folder(
      megatoy::system::PathService::web_storage_root(),
      [this, on_changed](platform::web::FolderImportResult result) {
        if (result.cancelled) {
          return;
        }
        if (!result.ok) {
          megatoy::status::error("Folder import failed: " + result.error);
          return;
        }
        if (add_workspace_folder(result.path)) {
          if (result.filtered_count == 0 &&
              result.validation_failures.empty()) {
            megatoy::status::success(
                "Imported \"" + result.folder_name + "\" (" +
                std::to_string(result.file_count) + " files)");
          }
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
  add_chosen_workspace_folder(chosen, std::move(on_changed),
                              std::move(confirm));
#endif
}

#if !defined(MEGATOY_PLATFORM_WEB)
void PreferenceManager::add_chosen_workspace_folder(
    const std::filesystem::path &chosen, std::function<void()> on_changed,
    ConfirmReplace confirm) {
  // Warm the parse cache on a worker before the folder joins the workspace:
  // the sync that follows re-walks it on the UI thread, and for a folder of
  // thousands of banks that first parse is a freeze measured in seconds.
  // Warm the same path the workspace will store, or the cache keys miss.
  const auto folder = megatoy::workspace::normalize_path(chosen);
  const std::string folder_name = folder.filename().string();
  if (const auto *outer = workspace_.enclosing(folder)) {
    megatoy::status::warning("\"" + folder_name +
                             "\" is already in the workspace, inside \"" +
                             outer->name + "\".");
    return;
  }
  const auto inside = workspace_.enclosed_by(folder);
  std::string replaced;
  for (const auto *inner : inside) {
    if (workspace_folder_is_protected(inner->path)) {
      megatoy::status::warning("\"" + folder_name + "\" holds \"" +
                               inner->name +
                               "\", which cannot leave the workspace.");
      return;
    }
    replaced += (replaced.empty() ? "\"" : ", \"") + inner->name + "\"";
  }

  auto add = [this, folder, folder_name, on_changed]() {
    const bool started = patches::background_folder_scan::begin(
        folder, [this, folder, folder_name, on_changed](bool cancelled) {
          if (cancelled) {
            megatoy::status::info("Folder scan cancelled.");
            return;
          }
          if (add_workspace_folder(folder)) {
            if (on_changed) {
              on_changed();
            }
          } else {
            megatoy::status::warning("\"" + folder_name +
                                     "\" is already in the workspace.");
          }
        });
    if (!started) {
      megatoy::status::warning("Another folder scan is in progress.");
    }
  };
  if (inside.empty()) {
    add();
    return;
  }
  if (!confirm) {
    megatoy::status::warning("\"" + folder_name +
                             "\" holds folders already in the workspace.");
    return;
  }
  confirm("\"" + folder_name + "\" holds " + replaced +
              ", already in the workspace. Merge " +
              (inside.size() == 1 ? "it" : "them") + " into \"" +
              folder_name + "\"?\n\nRatings and categories are kept.",
          std::move(add));
}
#endif

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

const std::string &PreferenceManager::last_patch_path() const {
  return last_patch_path_;
}

void PreferenceManager::set_last_patch_path(const std::string &path) {
  if (path == last_patch_path_) {
    return;
  }
  last_patch_path_ = path;
  save_preferences();
}

const std::vector<OpenPatchRecord> &PreferenceManager::open_patches() const {
  return open_patches_;
}

int PreferenceManager::active_open_patch() const { return active_open_patch_; }

void PreferenceManager::set_open_patches(std::vector<OpenPatchRecord> patches,
                                         int active) {
  if (patches == open_patches_ && active == active_open_patch_) {
    return;
  }
  open_patches_ = std::move(patches);
  active_open_patch_ = active;
  save_preferences();
}

const std::string &PreferenceManager::last_seen_version() const {
  return last_seen_version_;
}

void PreferenceManager::set_last_seen_version(const std::string &version) {
  if (version == last_seen_version_) {
    return;
  }
  last_seen_version_ = version;
  save_preferences();
}

const std::string &PreferenceManager::layout() const { return layout_; }

void PreferenceManager::set_layout(const std::string &name) {
  if (name == layout_) {
    return;
  }
  layout_ = name;
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

  apply_loaded_data(data);
  return true;
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
  data.last_patch_path = last_patch_path_;
  data.open_patches = open_patches_;
  data.active_open_patch = active_open_patch_;
  data.last_seen_version = last_seen_version_;
  data.layout = layout_;
  data.show_builtin_presets = show_builtin_presets_;
  data.theme = theme_;
  data.ui_preferences = ui_preferences_;
  return data;
}

void PreferenceManager::apply_loaded_data(const PreferenceData &data) {
  workspace_.set_paths(data.workspace_folders);
  last_save_directory_ = data.last_save_directory;
  last_patch_path_ = data.last_patch_path;
  open_patches_ = data.open_patches;
  active_open_patch_ = data.active_open_patch;
  last_seen_version_ = data.last_seen_version;
  layout_ = data.layout;
  show_builtin_presets_ = data.show_builtin_presets;
  theme_ = data.theme;
  ui_preferences_ = data.ui_preferences;
}
