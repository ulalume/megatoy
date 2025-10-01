#include "data_paths.hpp"

#include <filesystem>
#include <iostream>

namespace megatoy::system {

namespace {
std::filesystem::path ensure_path(const std::filesystem::path &path) {
  return path.lexically_normal();
}
} // namespace

DataPathProvider::DataPathProvider() {
  const auto default_root = PathResolver::default_data_directory();
  set_data_root(default_root);
  paths_.builtin_presets_root = PathResolver::builtin_presets_directory();
  paths_.preferences_file = PathResolver::preferences_file_path();
  paths_.imgui_ini_file = PathResolver::imgui_ini_file_path();
}

void DataPathProvider::set_data_root(const std::filesystem::path &root) {
  paths_.data_root = ensure_path(root);
  paths_.patches_root = ensure_path(patches_directory(paths_.data_root));
  paths_.user_patches_root = ensure_path(user_patches_directory(paths_.data_root));
  paths_.export_root = ensure_path(export_directory(paths_.data_root));
}

bool DataPathProvider::ensure_directories() const {
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

std::filesystem::path
DataPathProvider::patches_directory(const std::filesystem::path &root) {
  return root / "patches";
}

std::filesystem::path
DataPathProvider::user_patches_directory(const std::filesystem::path &root) {
  return root / "patches" / "user";
}

std::filesystem::path
DataPathProvider::export_directory(const std::filesystem::path &root) {
  return root / "export";
}

} // namespace megatoy::system
