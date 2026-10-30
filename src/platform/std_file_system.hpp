#pragma once

#include <filesystem>
#include <ios>
#include <istream>
#include <memory>
#include <ostream>
#include <system_error>
#include <vector>

namespace platform {

struct DirectoryEntry {
  std::filesystem::path path;
  bool is_directory = false;
  bool is_regular_file = false;
};

// A std::filesystem-backed file system used by native and web builds.
class StdFileSystem {
public:
  bool exists(const std::filesystem::path &path) const;
  bool is_directory(const std::filesystem::path &path) const;
  bool is_directory(const std::filesystem::path &path,
                    std::error_code &error) const;
  bool create_directories(const std::filesystem::path &path);
  std::vector<DirectoryEntry>
  read_directory(const std::filesystem::path &path) const;
  bool last_write_time(const std::filesystem::path &path,
                       std::filesystem::file_time_type &result) const;
  bool file_size(const std::filesystem::path &path,
                 std::uintmax_t &result) const;
  std::unique_ptr<std::istream>
  open_read(const std::filesystem::path &path) const;
  std::unique_ptr<std::ostream>
  open_write(const std::filesystem::path &path,
             std::ios::openmode mode = std::ios::binary);
};

} // namespace platform
