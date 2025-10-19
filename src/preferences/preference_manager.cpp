#include "preference_manager.hpp"

#include "platform/file_dialog.hpp"
#include "preference_storage.hpp"
#include <cstdlib>
#include <iostream>

PreferenceManager::PreferenceManager(megatoy::system::PathService &paths)
    : paths_(paths), directories_initialized(false),
      theme_(ui::styles::ThemeId::MegatoyDark),
      storage_(make_json_preference_storage(get_preferences_file_path())) {
  paths_.set_data_root(get_default_data_directory());
  load_preferences();
  ensure_directories_exist();
}

PreferenceManager::~PreferenceManager() { platform::file_dialog::shutdown(); }

std::filesystem::path PreferenceManager::get_default_data_directory() const {
  return megatoy::system::PathService::default_data_directory();
}

void PreferenceManager::set_data_directory(const std::filesystem::path &path) {
  paths_.set_data_root(path);
  ensure_directories_exist();
  save_preferences();
}

std::filesystem::path PreferenceManager::get_data_directory() const {
  return paths_.paths().data_root;
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

void PreferenceManager::reset_data_directory() {
  paths_.set_data_root(get_default_data_directory());
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
  data.data_directory = paths_.paths().data_root;
  data.theme = theme_;
  data.ui_preferences = ui_preferences_;
  return data;
}

void PreferenceManager::apply_loaded_data(const PreferenceData &data) {
  paths_.set_data_root(data.data_directory);
  theme_ = data.theme;
  ui_preferences_ = data.ui_preferences;
}
