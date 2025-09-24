#include "preference_manager.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

#ifdef __APPLE__
#include <cstdio>
#else
#include <nfd.h>
#endif

PreferenceManager::PreferenceManager()
    : data_directory(get_default_data_directory()),
      directories_initialized(false), nfd_initialized(false) {
  // Load saved preferences
  load_preferences();

  // Create required directories
  ensure_directories_exist();
}

PreferenceManager::~PreferenceManager() {
#ifndef __APPLE__
  if (nfd_initialized) {
    NFD_Quit();
    nfd_initialized = false;
  }
#endif
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

namespace {
#ifdef __APPLE__
std::string escape_for_applescript(const std::string &input) {
  std::string escaped;
  escaped.reserve(input.size() * 2);
  for (char c : input) {
    if (c == '\\' || c == '"') {
      escaped.push_back('\\');
    }
    escaped.push_back(c);
  }
  return escaped;
}

bool choose_directory_with_applescript(
    const std::filesystem::path &default_path,
    std::filesystem::path &selected_path) {
  std::string default_utf8 =
      default_path.empty() ? std::filesystem::path(std::getenv("HOME")).string()
                           : default_path.string();

  std::string script = "set defaultPath to POSIX file \"" +
                       escape_for_applescript(default_utf8) + "\"\n";
  script += "set chosenFolder to choose folder with prompt \"Select data "
            "directory\" default location defaultPath\n";
  script += "POSIX path of chosenFolder\n";

  std::string command =
      "osascript 2>/dev/null <<'APPLESCRIPT'\n" + script + "APPLESCRIPT\n";

  FILE *pipe = popen(command.c_str(), "r");
  if (!pipe) {
    std::cerr << "Failed to invoke osascript" << std::endl;
    return false;
  }

  std::string output;
  char buffer[256];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output += buffer;
  }

  int status = pclose(pipe);
  if (status == 0 && !output.empty()) {
    while (!output.empty() &&
           (output.back() == '\n' || output.back() == '\r')) {
      output.pop_back();
    }
    if (!output.empty()) {
      selected_path = std::filesystem::path(output);
      return true;
    }
  }

  std::cerr << "osascript choose folder failed with status " << status
            << std::endl;
  return false;
}
#endif
} // namespace

bool PreferenceManager::select_data_directory() {
  std::cerr << "PreferenceManager::select_data_directory invoked" << std::endl;
#ifdef __APPLE__
  std::filesystem::path chosen_path;
  if (choose_directory_with_applescript(data_directory, chosen_path)) {
    set_data_directory(chosen_path);
    std::cerr << "Directory selection succeeded (AppleScript)" << std::endl;
    return true;
  }
  std::cerr << "Directory selection cancelled or failed (AppleScript)"
            << std::endl;
  return false;
#else
  if (!ensure_nfd_initialized()) {
    std::cerr << "Native File Dialog not initialized" << std::endl;
    return false;
  }

  nfdchar_t *outPath;
  nfdresult_t result = NFD_PickFolder(&outPath, data_directory.c_str());
  std::cerr << "NFD_PickFolder result=" << static_cast<int>(result)
            << std::endl;

  if (result == NFD_OKAY) {
    set_data_directory(std::filesystem::path(outPath));
    NFD_FreePath(outPath);
    std::cerr << "Directory selection succeeded" << std::endl;
    return true;
  } else if (result == NFD_CANCEL) {
    std::cerr << "Directory selection cancelled" << std::endl;
    return false;
  } else {
    std::cerr << "Error selecting directory: " << NFD_GetError() << std::endl;
    return false;
  }
#endif
}

bool PreferenceManager::ensure_directories_exist() {
  try {
    std::filesystem::create_directories(data_directory);
    std::filesystem::create_directories(get_user_patches_directory());
    std::filesystem::create_directories(get_patches_directory());

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
#ifdef __APPLE__
  return directories_initialized;
#else
  return directories_initialized && nfd_initialized;
#endif
}

bool PreferenceManager::initialize_file_dialog() {
#ifdef __APPLE__
  return true;
#else
  return ensure_nfd_initialized();
#endif
}

bool PreferenceManager::ensure_nfd_initialized() {
#ifdef __APPLE__
  return true;
#else
  if (nfd_initialized) {
    return true;
  }

  std::cerr << "Attempting NFD_Init" << std::endl;
  if (NFD_Init() == NFD_OKAY) {
    nfd_initialized = true;
    std::cerr << "NFD_Init succeeded" << std::endl;
    return true;
  }

  const char *error = NFD_GetError();
  if (error != nullptr) {
    std::cerr << "Failed to initialize Native File Dialog: " << error
              << std::endl;
  } else {
    std::cerr << "Failed to initialize Native File Dialog" << std::endl;
  }
  return false;
#endif
}
