#pragma once

#include "path_resolver.hpp"

#include <filesystem>

namespace megatoy::system {

struct DataPaths {
  std::filesystem::path data_root;
  std::filesystem::path patches_root;
  std::filesystem::path user_patches_root;
  std::filesystem::path export_root;
  std::filesystem::path builtin_presets_root;
  std::filesystem::path preferences_file;
  std::filesystem::path imgui_ini_file;
};

class DataPathProvider {
public:
  DataPathProvider();

  const DataPaths &paths() const { return paths_; }

  void set_data_root(const std::filesystem::path &root);
  bool ensure_directories() const;

private:
  DataPaths paths_;

  static std::filesystem::path patches_directory(const std::filesystem::path &root);
  static std::filesystem::path user_patches_directory(const std::filesystem::path &root);
  static std::filesystem::path export_directory(const std::filesystem::path &root);
};

} // namespace megatoy::system
