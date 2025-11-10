#pragma once

#include "platform/virtual_file_system.hpp"

namespace platform::web {

class WebFileSystem : public VirtualFileSystem {
public:
  WebFileSystem() = default;
  ~WebFileSystem() override = default;

  bool exists(const std::filesystem::path &path) const override;
  bool is_directory(const std::filesystem::path &path) const override;
  bool create_directories(const std::filesystem::path &path) override;
  std::vector<DirectoryEntry>
  read_directory(const std::filesystem::path &path) const override;
  bool last_write_time(const std::filesystem::path &path,
                       std::filesystem::file_time_type &result) const override;
  std::unique_ptr<std::istream>
  open_read(const std::filesystem::path &path) const override;
  std::unique_ptr<std::ostream> open_write(const std::filesystem::path &path,
                                           std::ios::openmode mode) override;
};

} // namespace platform::web
