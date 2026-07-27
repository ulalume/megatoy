#include "preference_manager.hpp"

#include "platform/file_dialog.hpp"
#include "preference_storage.hpp"
#include <cstdlib>
#include <system_error>
#include <iostream>

PreferenceManager::PreferenceManager(megatoy::system::PathService &paths)
    : paths_(paths), directories_initialized(false),
      theme_(ui::styles::ThemeId::MegatoyDark),
      storage_(make_json_preference_storage(get_preferences_file_path(),
                                            paths_.file_system())) {
  load_preferences();
  ensure_directories_exist();
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
  if (!workspace_.remove(path)) {
    return false;
  }
  save_preferences();
  return true;
}

bool PreferenceManager::reorder_workspace_folder(std::size_t from,
                                                 std::size_t to) {
  if (!workspace_.reorder(from, to)) {
    return false;
  }
  save_preferences();
  return true;
}

bool PreferenceManager::prompt_add_workspace_folder() {
  std::filesystem::path chosen;
  const auto start = last_save_directory();
  if (platform::file_dialog::pick_folder(start, chosen) !=
      platform::file_dialog::DialogResult::Ok) {
    return false;
  }
  return add_workspace_folder(chosen);
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

  apply_loaded_data(data);
  return true;
}

void PreferenceManager::reset_ui_preferences() {
  const auto current = ui_preferences_;
  ui_preferences_ = {};
  ui_preferences_.use_velocity = current.use_velocity;
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
  data.theme = theme_;
  data.ui_preferences = ui_preferences_;
  return data;
}

void PreferenceManager::apply_loaded_data(const PreferenceData &data) {
  workspace_.set_paths(data.workspace_folders);
  last_save_directory_ = data.last_save_directory;
  show_builtin_presets_ = data.show_builtin_presets;
  theme_ = data.theme;
  ui_preferences_ = data.ui_preferences;
}
