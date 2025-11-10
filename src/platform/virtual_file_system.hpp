#pragma once

#include <filesystem>
#include <ios>
#include <istream>
#include <memory>
#include <ostream>
#include <vector>

namespace platform {

struct DirectoryEntry {
  std::filesystem::path path;
  bool is_directory = false;
  bool is_regular_file = false;
};

class VirtualFileSystem {
public:
  virtual ~VirtualFileSystem() = default;

  virtual bool exists(const std::filesystem::path &path) const = 0;
  virtual bool is_directory(const std::filesystem::path &path) const = 0;
  virtual bool create_directories(const std::filesystem::path &path) = 0;
  virtual std::vector<DirectoryEntry>
  read_directory(const std::filesystem::path &path) const = 0;
  virtual bool
  last_write_time(const std::filesystem::path &path,
                  std::filesystem::file_time_type &result) const = 0;
  virtual std::unique_ptr<std::istream>
  open_read(const std::filesystem::path &path) const = 0;
  virtual std::unique_ptr<std::ostream>
  open_write(const std::filesystem::path &path,
             std::ios::openmode mode = std::ios::binary) = 0;
};

} // namespace platform
