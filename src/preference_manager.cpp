#include "preference_manager.hpp"

#include "platform/file_dialog.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

PreferenceManager::PreferenceManager()
    : data_directory(get_default_data_directory()),
      directories_initialized(false),
      theme_(ui::styles::ThemeId::MegatoyDark) {
  load_preferences();
  ensure_directories_exist();
}

PreferenceManager::~PreferenceManager() { platform::file_dialog::shutdown(); }

std::filesystem::path PreferenceManager::get_default_data_directory() const {
#ifdef _WIN32
  const char *userprofile = std::getenv("USERPROFILE");
  if (userprofile) {
    return std::filesystem::path(userprofile) / "Documents" / "megatoy";
  }
#else
  const char *home = std::getenv("HOME");
  if (home) {
    return std::filesystem::path(home) / "Documents" / "megatoy";
  }
#endif
  return std::filesystem::current_path() / "megatoy";
}

void PreferenceManager::set_data_directory(const std::filesystem::path &path) {
  data_directory = path;
  ensure_directories_exist();
  save_preferences();
}

std::filesystem::path PreferenceManager::get_data_directory() const {
  return data_directory;
}

std::filesystem::path PreferenceManager::get_patches_directory() const {
  return data_directory / "patches";
}

std::filesystem::path PreferenceManager::get_user_patches_directory() const {
  return data_directory / "patches/user";
}

std::filesystem::path PreferenceManager::get_export_directory() const {
  return data_directory / "export";
}

bool PreferenceManager::select_data_directory() {
  std::filesystem::path chosen_path;
  auto result = platform::file_dialog::pick_folder(data_directory, chosen_path);
  if (result == platform::file_dialog::DialogResult::Ok) {
    set_data_directory(chosen_path);
    return true;
  }
  return result != platform::file_dialog::DialogResult::Error;
}

bool PreferenceManager::ensure_directories_exist() {
  try {
    std::filesystem::create_directories(data_directory);
    std::filesystem::create_directories(get_user_patches_directory());
    std::filesystem::create_directories(get_patches_directory());
    std::filesystem::create_directories(get_export_directory());

    directories_initialized = true;
    return true;
  } catch (const std::filesystem::filesystem_error &e) {
    std::cerr << "Failed to create directories: " << e.what() << std::endl;
    directories_initialized = false;
    return false;
  }
}

std::filesystem::path PreferenceManager::get_preferences_file_path() const {
#ifdef _WIN32
  const char *appdata = std::getenv("APPDATA");
  if (appdata) {
    return std::filesystem::path(appdata) / "megatoy" / "preferences.json";
  }
#else
  const char *home = std::getenv("HOME");
  if (home) {
    return std::filesystem::path(home) / ".config" / "megatoy" /
           "preferences.json";
  }
#endif
  return std::filesystem::current_path() / "preferences.json";
}

std::filesystem::path PreferenceManager::get_imgui_ini_file() const {
#ifdef _WIN32
  const char *appdata = std::getenv("APPDATA");
  if (appdata) {
    return std::filesystem::path(appdata) / "megatoy" / "imgui.ini";
  }
#else
  const char *home = std::getenv("HOME");
  if (home) {
    return std::filesystem::path(home) / ".config" / "megatoy" / "imgui.ini";
  }
#endif
  return std::filesystem::current_path() / "imgui.ini";
}

bool PreferenceManager::save_preferences() {
  try {
    auto prefs_path = get_preferences_file_path();
    std::filesystem::create_directories(prefs_path.parent_path());

    nlohmann::json j;
    j["data_directory"] = data_directory.string();
    j["theme"] = ui::styles::storage_key(theme_);

    std::ofstream file(prefs_path);
    if (!file) {
      std::cerr << "Failed to open preferences file for writing" << std::endl;
      return false;
    }

    file << j.dump(2);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "Error saving preferences: " << e.what() << std::endl;
    return false;
  }
}

bool PreferenceManager::load_preferences() {
  try {
    auto prefs_path = get_preferences_file_path();

    if (!std::filesystem::exists(prefs_path)) {
      return true;
    }

    std::ifstream file(prefs_path);
    if (!file) {
      std::cerr << "Failed to open preferences file for reading" << std::endl;
      return false;
    }

    nlohmann::json j;
    file >> j;

    if (j.contains("data_directory")) {
      data_directory = j["data_directory"].get<std::string>();
    }

    if (j.contains("theme")) {
      theme_ = ui::styles::theme_id_from_storage_key(
          j["theme"].get<std::string>(), ui::styles::ThemeId::MegatoyDark);
    }

    return true;
  } catch (const std::exception &e) {
    std::cerr << "Error loading preferences: " << e.what() << std::endl;
    return false;
  }
}

void PreferenceManager::reset_to_defaults() {
  data_directory = get_default_data_directory();
  ensure_directories_exist();
  theme_ = ui::styles::ThemeId::MegatoyDark;
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
