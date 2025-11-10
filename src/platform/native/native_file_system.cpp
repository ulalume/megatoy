#include "platform/native/native_file_system.hpp"

#include <fstream>
#include <system_error>

bool NativeFileSystem::exists(const std::filesystem::path &path) const {
  return std::filesystem::exists(path);
}

bool NativeFileSystem::is_directory(const std::filesystem::path &path) const {
  return std::filesystem::is_directory(path);
}

bool NativeFileSystem::create_directories(const std::filesystem::path &path) {
  try {
    std::filesystem::create_directories(path);
    return true;
  } catch (...) {
    return false;
  }
}

std::vector<platform::DirectoryEntry>
NativeFileSystem::read_directory(const std::filesystem::path &path) const {
  std::vector<platform::DirectoryEntry> entries;
  if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path)) {
    return entries;
  }

  try {
    for (const auto &entry : std::filesystem::directory_iterator(path)) {
      platform::DirectoryEntry info;
      info.path = entry.path();
      info.is_directory = entry.is_directory();
      info.is_regular_file = entry.is_regular_file();
      entries.push_back(std::move(info));
    }
  } catch (...) {
    entries.clear();
  }
  return entries;
}

bool NativeFileSystem::last_write_time(
    const std::filesystem::path &path,
    std::filesystem::file_time_type &result) const {
  try {
    result = std::filesystem::last_write_time(path);
    return true;
  } catch (...) {
    return false;
  }
}

std::unique_ptr<std::istream>
NativeFileSystem::open_read(const std::filesystem::path &path) const {
  auto stream = std::make_unique<std::ifstream>(path, std::ios::binary);
  if (!*stream) {
    return nullptr;
  }
  return stream;
}

std::unique_ptr<std::ostream>
NativeFileSystem::open_write(const std::filesystem::path &path,
                             std::ios::openmode mode) {
  if (path.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
  }
  auto stream = std::make_unique<std::ofstream>(path, std::ios::binary | mode);
  if (!*stream) {
    return nullptr;
  }
  return stream;
}
