#include "path_service.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
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

namespace megatoy::system {

// Static implementations
std::filesystem::path PathService::executable_directory_impl() {
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

std::filesystem::path
PathService::canonical_or_normal(const std::filesystem::path &path) {
  try {
    return std::filesystem::weakly_canonical(path);
  } catch (const std::filesystem::filesystem_error &) {
    return path.lexically_normal();
  }
}

std::filesystem::path
PathService::normalize(const std::filesystem::path &path) {
  return path.lexically_normal();
}

std::filesystem::path
PathService::patches_directory(const std::filesystem::path &root) {
  return root / "patches";
}

std::filesystem::path
PathService::user_patches_directory(const std::filesystem::path &root) {
  return root / "patches" / "user";
}

std::filesystem::path
PathService::export_directory(const std::filesystem::path &root) {
  return root / "export";
}

// Public static methods
std::filesystem::path PathService::executable_directory() {
  static const std::filesystem::path kExecutableDir =
      executable_directory_impl();
  return kExecutableDir;
}

std::filesystem::path PathService::builtin_presets_directory() {
  const auto executable_dir = executable_directory();
  const auto combined = executable_dir / MEGATOY_PRESETS_RELATIVE_PATH;
  return canonical_or_normal(combined);
}

std::filesystem::path PathService::default_data_directory() {
#if defined(_WIN32)
  if (const char *userprofile = std::getenv("USERPROFILE")) {
    return std::filesystem::path(userprofile) / "Documents" / "megatoy";
  }
#else
  if (const char *home = std::getenv("HOME")) {
    return std::filesystem::path(home) / "Documents" / "megatoy";
  }
#endif
  return std::filesystem::current_path() / "megatoy";
}

std::filesystem::path PathService::preferences_file_path() {
#if defined(_WIN32)
  if (const char *appdata = std::getenv("APPDATA")) {
    return std::filesystem::path(appdata) / "megatoy" / "preferences.json";
  }
#else
  if (const char *home = std::getenv("HOME")) {
    return std::filesystem::path(home) / ".config" / "megatoy" /
           "preferences.json";
  }
#endif
  return std::filesystem::current_path() / "preferences.json";
}

std::filesystem::path PathService::imgui_ini_file_path() {
#if defined(_WIN32)
  if (const char *appdata = std::getenv("APPDATA")) {
    return std::filesystem::path(appdata) / "megatoy" / "imgui.ini";
  }
#else
  if (const char *home = std::getenv("HOME")) {
    return std::filesystem::path(home) / ".config" / "megatoy" / "imgui.ini";
  }
#endif
  return std::filesystem::current_path() / "imgui.ini";
}

std::filesystem::path PathService::patch_metadata_db_path() {
#if defined(_WIN32)
  if (const char *appdata = std::getenv("APPDATA")) {
    return std::filesystem::path(appdata) / "megatoy" / "patch_metadata.db";
  }
#else
  if (const char *home = std::getenv("HOME")) {
    return std::filesystem::path(home) / ".config" / "megatoy" /
           "patch_metadata.db";
  }
#endif
  return std::filesystem::current_path() / "patch_metadata.db";
}

// Constructor and instance methods
PathService::PathService() {
  set_data_root(default_data_directory());
  paths_.builtin_presets_root = builtin_presets_directory();
  paths_.preferences_file = preferences_file_path();
  paths_.imgui_ini_file = imgui_ini_file_path();
  paths_.patch_metadata_db = patch_metadata_db_path();
}

void PathService::set_data_root(const std::filesystem::path &root) {
  paths_.data_root = normalize(root);
  paths_.patches_root = patches_directory(paths_.data_root);
  paths_.user_patches_root = user_patches_directory(paths_.data_root);
  paths_.export_root = export_directory(paths_.data_root);
}

bool PathService::ensure_directories() const {
  try {
    std::filesystem::create_directories(paths_.data_root);
    std::filesystem::create_directories(paths_.patches_root);
    std::filesystem::create_directories(paths_.user_patches_root);
    std::filesystem::create_directories(paths_.export_root);
    // Ensure config directory exists for metadata database
    std::filesystem::create_directories(paths_.patch_metadata_db.parent_path());
    return true;
  } catch (const std::filesystem::filesystem_error &e) {
    std::cerr << "Failed to create application directories: " << e.what()
              << '\n';
    return false;
  }
}

} // namespace megatoy::system
