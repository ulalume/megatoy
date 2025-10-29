#pragma once

#include <filesystem>

namespace megatoy::system {

struct DirectoryPaths {
  std::filesystem::path data_root;
  std::filesystem::path patches_root;
  std::filesystem::path user_patches_root;
  std::filesystem::path export_root;
  std::filesystem::path builtin_presets_root;
  std::filesystem::path preferences_file;
  std::filesystem::path imgui_ini_file;
  std::filesystem::path patch_metadata_db;
};

class PathService {
public:
  PathService();

  // Directory paths access
  const DirectoryPaths &paths() const { return paths_; }

  // Directory management
  void set_data_root(const std::filesystem::path &root);
  bool ensure_directories() const;

  // Static path resolution methods
  static std::filesystem::path executable_directory();
  static std::filesystem::path builtin_presets_directory();
  static std::filesystem::path default_data_directory();
  static std::filesystem::path preferences_file_path();
  static std::filesystem::path imgui_ini_file_path();
  static std::filesystem::path patch_metadata_db_path();

private:
  DirectoryPaths paths_;

  // Internal path utilities
  static std::filesystem::path normalize(const std::filesystem::path &path);
  static std::filesystem::path
  canonical_or_normal(const std::filesystem::path &path);

  // Directory builders
  static std::filesystem::path
  patches_directory(const std::filesystem::path &root);
  static std::filesystem::path
  user_patches_directory(const std::filesystem::path &root);
  static std::filesystem::path
  export_directory(const std::filesystem::path &root);

  // Platform-specific implementations
  static std::filesystem::path executable_directory_impl();
};

} // namespace megatoy::system
