#include "platform/web/web_file_system.hpp"

#include <fstream>
#include <system_error>

namespace platform::web {

bool WebFileSystem::exists(const std::filesystem::path &path) const {
  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

bool WebFileSystem::is_directory(const std::filesystem::path &path) const {
  std::error_code ec;
  return std::filesystem::is_directory(path, ec);
}

bool WebFileSystem::create_directories(const std::filesystem::path &path) {
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  return !ec;
}

std::vector<DirectoryEntry>
WebFileSystem::read_directory(const std::filesystem::path &path) const {
  std::vector<DirectoryEntry> entries;
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) ||
      !std::filesystem::is_directory(path, ec)) {
    return entries;
  }

  for (const auto &entry : std::filesystem::directory_iterator(path, ec)) {
    if (ec) {
      entries.clear();
      return entries;
    }
    DirectoryEntry info;
    info.path = entry.path();
    info.is_directory = entry.is_directory();
    info.is_regular_file = entry.is_regular_file();
    entries.push_back(std::move(info));
  }
  return entries;
}

bool WebFileSystem::last_write_time(
    const std::filesystem::path &path,
    std::filesystem::file_time_type &result) const {
  std::error_code ec;
  result = std::filesystem::last_write_time(path, ec);
  return !ec;
}

std::unique_ptr<std::istream>
WebFileSystem::open_read(const std::filesystem::path &path) const {
  auto stream = std::make_unique<std::ifstream>(path, std::ios::binary);
  if (!*stream) {
    return nullptr;
  }
  return stream;
}

std::unique_ptr<std::ostream>
WebFileSystem::open_write(const std::filesystem::path &path,
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

} // namespace platform::web
