#include "directory_service.hpp"
#include "path_resolver.hpp"

#include <filesystem>
#include <iostream>

namespace megatoy::system {

std::filesystem::path
DirectoryService::normalize(const std::filesystem::path &path) {
  return path.lexically_normal();
}

std::filesystem::path
DirectoryService::patches_directory(const std::filesystem::path &root) {
  return root / "patches";
}

std::filesystem::path
DirectoryService::user_patches_directory(const std::filesystem::path &root) {
  return root / "patches" / "user";
}

std::filesystem::path
DirectoryService::export_directory(const std::filesystem::path &root) {
  return root / "export";
}

DirectoryService::DirectoryService() {
  set_data_root(PathResolver::default_data_directory());
  paths_.builtin_presets_root = PathResolver::builtin_presets_directory();
  paths_.preferences_file = PathResolver::preferences_file_path();
  paths_.imgui_ini_file = PathResolver::imgui_ini_file_path();
}

void DirectoryService::set_data_root(const std::filesystem::path &root) {
  paths_.data_root = normalize(root);
  paths_.patches_root = patches_directory(paths_.data_root);
  paths_.user_patches_root = user_patches_directory(paths_.data_root);
  paths_.export_root = export_directory(paths_.data_root);
}

bool DirectoryService::ensure_directories() const {
  try {
    std::filesystem::create_directories(paths_.data_root);
    std::filesystem::create_directories(paths_.patches_root);
    std::filesystem::create_directories(paths_.user_patches_root);
    std::filesystem::create_directories(paths_.export_root);
    return true;
  } catch (const std::filesystem::filesystem_error &e) {
    std::cerr << "Failed to create application directories: " << e.what()
              << '\n';
    return false;
  }
}

} // namespace megatoy::system
