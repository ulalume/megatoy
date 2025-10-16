#include "gin.hpp"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace formats::gin {

std::filesystem::path build_patch_path(const std::filesystem::path &patches_dir,
                                       const std::string &filename) {
  std::string full_filename = filename;
  if (full_filename.find(".gin") == std::string::npos) {
    full_filename += ".gin";
  }
  return patches_dir / full_filename;
}

const std::optional<std::filesystem::path>
save_patch(const std::filesystem::path &patches_dir, const ym2612::Patch &patch,
           const std::string &filename) {
  try {
    auto filepath = build_patch_path(patches_dir, filename);
    nlohmann::json j = patch;

    std::ofstream file(filepath);
    if (!file) {
      std::cerr << "Failed to open file for writing: " << filepath << std::endl;
      return std::nullopt;
    }

    file << j.dump(2);
    std::cout << "Saved patch to: " << filepath << std::endl;
    return filepath;
  } catch (const std::exception &e) {
    std::cerr << "Save error: " << e.what() << std::endl;
    return std::nullopt;
  }
}

std::vector<ym2612::Patch> read_file(const std::filesystem::path &file_path) {
  try {
    if (!std::filesystem::exists(file_path)) {
      std::cerr << "File does not exist: " << file_path << std::endl;
      return {};
    }

    std::ifstream file(file_path);
    if (!file) {
      std::cerr << "Failed to open file for reading: " << file_path
                << std::endl;
      return {};
    }

    nlohmann::json j;
    file >> j;

    ym2612::Patch patch = j.get<ym2612::Patch>();

    std::cout << "Loaded patch from: " << file_path << std::endl;
    return {patch};
  } catch (const std::exception &e) {
    std::cerr << "Load error: " << e.what() << std::endl;
    return {};
  }
}

} // namespace formats::gin
