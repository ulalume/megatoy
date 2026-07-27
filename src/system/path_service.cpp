#include "path_service.hpp"

#include "platform/native/native_file_system.hpp"

#include "platform/platform_config.hpp"

#include <array>
#include <cstdlib>
#include <system_error>
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

namespace {

platform::VirtualFileSystem &default_vfs() {
  static NativeFileSystem fs;
  return fs;
}

} // namespace

namespace megatoy::system {

namespace fs = std::filesystem;

static fs::path home_path() {
#if defined(MEGATOY_PLATFORM_WEB)
  return fs::path("/app");
#else
#if defined(_WIN32)
  if (const char *user = std::getenv("USERPROFILE"))
    return user;
  if (const char *appdata = std::getenv("APPDATA"))
    return appdata;
#else
  if (const char *home = std::getenv("HOME"))
    return home;
#endif
  return fs::current_path();
#endif
}

static fs::path config_path(std::string_view filename) {
#if defined(MEGATOY_PLATFORM_WEB)
  return fs::path("/app/config") / filename;
#else
#if defined(_WIN32)
  return home_path() / "megatoy" / filename;
#else
  return home_path() / ".config" / "megatoy" / filename;
#endif
#endif
}

fs::path PathService::executable_directory_impl() {
#if defined(_WIN32)
  std::wstring buffer(MAX_PATH, L'\0');
  DWORD len = ::GetModuleFileNameW(nullptr, buffer.data(),
                                   static_cast<DWORD>(buffer.size()));
  if (len == 0 || len >= buffer.size())
    return fs::current_path();
  buffer.resize(len);
  return fs::path(buffer).parent_path();
#elif defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string buffer(size, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) != 0)
    return fs::current_path();
  return fs::path(buffer.c_str()).parent_path();
#elif defined(MEGATOY_PLATFORM_WEB)
  return fs::path("/app");
#else
  std::array<char, PATH_MAX> buffer{};
  ssize_t len = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (len <= 0)
    return fs::current_path();
  buffer[static_cast<size_t>(len)] = '\0';
  return fs::path(buffer.data()).parent_path();
#endif
}

fs::path PathService::canonical_or_normal(const fs::path &p) {
  try {
    return fs::weakly_canonical(p);
  } catch (...) {
    return p.lexically_normal();
  }
}

fs::path PathService::executable_directory() {
  static const fs::path dir = executable_directory_impl();
  return dir;
}

fs::path PathService::builtin_presets_directory() {
#if defined(MEGATOY_PLATFORM_WEB)
  return fs::path("/app/assets/presets");
#else
  return canonical_or_normal(executable_directory() /
                             MEGATOY_PRESETS_RELATIVE_PATH);
#endif
}

fs::path PathService::web_storage_root() {
#if defined(MEGATOY_PLATFORM_WEB)
  // Must match the mount point in dist/web_pre.js.
  return fs::path("/megatoy");
#else
  return {};
#endif
}

fs::path PathService::default_documents_directory() {
#if defined(MEGATOY_PLATFORM_WEB)
  return web_storage_root();
#else
  const auto documents = home_path() / "Documents";
  std::error_code ec;
  if (fs::is_directory(documents, ec) && !ec) {
    return documents;
  }
  return home_path();
#endif
}

fs::path PathService::preferences_file_path() {
  return config_path("preferences.json");
}

fs::path PathService::imgui_ini_file_path() { return config_path("imgui.ini"); }

PathService::PathService() : PathService(default_vfs()) {}

PathService::PathService(::platform::VirtualFileSystem &vfs,
                         fs::path config_root)
    : vfs_(vfs) {
  paths_.builtin_presets_root = builtin_presets_directory();
  if (config_root.empty()) {
    paths_.preferences_file = preferences_file_path();
    paths_.imgui_ini_file = imgui_ini_file_path();
  } else {
    paths_.preferences_file = config_root / "preferences.json";
    paths_.imgui_ini_file = config_root / "imgui.ini";
  }
}

bool PathService::ensure_directories() const {
  if (!vfs_.create_directories(paths_.preferences_file.parent_path())) {
    std::cerr << "Failed to create the configuration directory at "
              << paths_.preferences_file.parent_path() << "\n";
    return false;
  }
  return true;
}

} // namespace megatoy::system
