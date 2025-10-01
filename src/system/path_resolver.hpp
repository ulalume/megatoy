#pragma once

#include <filesystem>

namespace megatoy::system {

class PathResolver {
public:
  static std::filesystem::path executable_directory();
  static std::filesystem::path builtin_presets_directory();

  static std::filesystem::path default_data_directory();
  static std::filesystem::path preferences_file_path();
  static std::filesystem::path imgui_ini_file_path();
};

} // namespace megatoy::system
