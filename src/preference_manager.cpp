#include "preference_manager.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <nfd.h>
#include <nlohmann/json.hpp>

PreferenceManager::PreferenceManager()
    : data_directory(get_default_data_directory()),
      directories_initialized(false), nfd_initialized(false) {
  // Initialize NFD
  if (NFD_Init() == NFD_OKAY) {
    nfd_initialized = true;
  } else {
    std::cerr << "Failed to initialize Native File Dialog" << std::endl;
  }

  // Load saved preferences
  load_preferences();

  // Create required directories
  ensure_directories_exist();
}

PreferenceManager::~PreferenceManager() {
  if (nfd_initialized) {
    NFD_Quit();
  }
}

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

std::filesystem::path PreferenceManager::get_projects_directory() const {
  return data_directory / "projects";
}

bool PreferenceManager::select_data_directory() {
  if (!nfd_initialized) {
    std::cerr << "Native File Dialog not initialized" << std::endl;
    return false;
  }

  nfdchar_t *outPath;
  nfdresult_t result = NFD_PickFolder(&outPath, data_directory.c_str());

  if (result == NFD_OKAY) {
    set_data_directory(std::filesystem::path(outPath));
    NFD_FreePath(outPath);
    return true;
  } else if (result == NFD_CANCEL) {
    // User cancelled
    return false;
  } else {
    // Error
    std::cerr << "Error selecting directory: " << NFD_GetError() << std::endl;
    return false;
  }
}

bool PreferenceManager::ensure_directories_exist() {
  try {
    std::filesystem::create_directories(data_directory);
    std::filesystem::create_directories(get_user_patches_directory());
    std::filesystem::create_directories(get_patches_directory());
    std::filesystem::create_directories(get_projects_directory());

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
    return std::filesystem::path(appdata) / "vgm-test" / "preferences.json";
  }
#else
  const char *home = std::getenv("HOME");
  if (home) {
    return std::filesystem::path(home) / ".config" / "vgm-test" /
           "preferences.json";
  }
#endif
  return std::filesystem::current_path() / "preferences.json";
}

bool PreferenceManager::save_preferences() {
  try {
    auto prefs_path = get_preferences_file_path();

    // Create the parent directory
    std::filesystem::create_directories(prefs_path.parent_path());

    nlohmann::json j;
    j["data_directory"] = data_directory.string();

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
      // Use defaults when the preferences file does not exist
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

    return true;
  } catch (const std::exception &e) {
    std::cerr << "Error loading preferences: " << e.what() << std::endl;
    return false;
  }
}

void PreferenceManager::reset_to_defaults() {
  data_directory = get_default_data_directory();
  ensure_directories_exist();
  save_preferences();
}

bool PreferenceManager::is_initialized() const {
  return directories_initialized && nfd_initialized;
}
