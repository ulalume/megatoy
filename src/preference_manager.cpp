#include "preference_manager.hpp"

#include "platform/file_dialog.hpp"
#include "preference_storage.hpp"
#include <cstdlib>
#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

#ifndef MEGATOY_PRESETS_RELATIVE_PATH
#define MEGATOY_PRESETS_RELATIVE_PATH "presets"
#endif

namespace {

std::filesystem::path executable_directory() {
#if defined(_WIN32)
  std::wstring buffer(MAX_PATH, L'\0');
  DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(),
                                      static_cast<DWORD>(buffer.size()));
  if (length == 0 || length >= buffer.size()) {
    return std::filesystem::current_path();
  }
  buffer.resize(length);
  return std::filesystem::path(buffer).parent_path();
#elif defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string buffer(size, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
    return std::filesystem::current_path();
  }
  return std::filesystem::path(buffer.c_str()).parent_path();
#else
  std::array<char, PATH_MAX> buffer{};
  ssize_t length = ::readlink("/proc/self/exe", buffer.data(),
                              static_cast<ssize_t>(buffer.size() - 1));
  if (length <= 0) {
    return std::filesystem::current_path();
  }
  buffer[static_cast<size_t>(length)] = '\0';
  return std::filesystem::path(buffer.data()).parent_path();
#endif
}

std::filesystem::path compute_builtin_presets_directory() {
  const std::filesystem::path relative_presets_path{
      MEGATOY_PRESETS_RELATIVE_PATH};
  auto executable_dir = executable_directory();
  const auto combined = executable_dir / relative_presets_path;
  try {
    return std::filesystem::weakly_canonical(combined);
  } catch (const std::filesystem::filesystem_error &) {
    return combined.lexically_normal();
  }
}

} // namespace

PreferenceManager::PreferenceManager()
    : data_directory(get_default_data_directory()),
      builtin_presets_directory_(compute_builtin_presets_directory()),
      directories_initialized(false), theme_(ui::styles::ThemeId::MegatoyDark),
      storage_(make_json_preference_storage(get_preferences_file_path())) {
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

std::filesystem::path PreferenceManager::get_builtin_presets_directory() const {
  return builtin_presets_directory_;
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
  data_directory = get_default_data_directory();
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
  data.data_directory = data_directory;
  data.theme = theme_;
  data.ui_preferences = ui_preferences_;
  return data;
}

void PreferenceManager::apply_loaded_data(const PreferenceData &data) {
  data_directory = data.data_directory;
  theme_ = data.theme;
  ui_preferences_ = data.ui_preferences;
}
