#include "preference_manager.hpp"

#include "platform/file_dialog.hpp"
#include "preference_storage.hpp"
#include "system/path_resolver.hpp"
#include <cstdlib>
#include <iostream>

using megatoy::system::PathResolver;

PreferenceManager::PreferenceManager()
    : directories_initialized(false), theme_(ui::styles::ThemeId::MegatoyDark),
      storage_(make_json_preference_storage(get_preferences_file_path())) {
  data_paths_.set_data_root(get_default_data_directory());
  load_preferences();
  ensure_directories_exist();
}

PreferenceManager::~PreferenceManager() { platform::file_dialog::shutdown(); }

std::filesystem::path PreferenceManager::get_default_data_directory() const {
  return PathResolver::default_data_directory();
}

void PreferenceManager::set_data_directory(const std::filesystem::path &path) {
  data_paths_.set_data_root(path);
  ensure_directories_exist();
  save_preferences();
}

std::filesystem::path PreferenceManager::get_data_directory() const {
  return data_paths_.paths().data_root;
}

std::filesystem::path PreferenceManager::get_patches_directory() const {
  return data_paths_.paths().patches_root;
}

std::filesystem::path PreferenceManager::get_user_patches_directory() const {
  return data_paths_.paths().user_patches_root;
}

std::filesystem::path PreferenceManager::get_export_directory() const {
  return data_paths_.paths().export_root;
}

std::filesystem::path PreferenceManager::get_builtin_presets_directory() const {
  return data_paths_.paths().builtin_presets_root;
}

bool PreferenceManager::select_data_directory() {
  std::filesystem::path chosen_path;
  auto result =
      platform::file_dialog::pick_folder(get_data_directory(), chosen_path);
  if (result == platform::file_dialog::DialogResult::Ok) {
    set_data_directory(chosen_path);
    return true;
  }
  return result != platform::file_dialog::DialogResult::Error;
}

bool PreferenceManager::ensure_directories_exist() {
  directories_initialized = data_paths_.ensure_directories();
  return directories_initialized;
}

std::filesystem::path PreferenceManager::get_preferences_file_path() const {
  return PathResolver::preferences_file_path();
}

std::filesystem::path PreferenceManager::get_imgui_ini_file() const {
  return PathResolver::imgui_ini_file_path();
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

void PreferenceManager::reset_data_directory() {
  data_paths_.set_data_root(get_default_data_directory());
  ensure_directories_exist();
  save_preferences();
}

void PreferenceManager::reset_ui_preferences() {
  ui_preferences_ = {};
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
  data.data_directory = data_paths_.paths().data_root;
  data.theme = theme_;
  data.ui_preferences = ui_preferences_;
  return data;
}

void PreferenceManager::apply_loaded_data(const PreferenceData &data) {
  data_paths_.set_data_root(data.data_directory);
  theme_ = data.theme;
  ui_preferences_ = data.ui_preferences;
}
