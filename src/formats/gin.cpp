#include "gin.hpp"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace ym2612::formats::gin {

std::filesystem::path build_patch_path(const std::filesystem::path &patches_dir,
                                       const std::string &filename) {
  std::string full_filename = filename;
  if (full_filename.find(".gin") == std::string::npos) {
    full_filename += ".gin";
  }
  return patches_dir / full_filename;
}

const std::optional<std::filesystem::path>
save_patch(const std::filesystem::path &patches_dir, const Patch &patch,
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

bool load_patch(const std::filesystem::path &patches_dir, Patch &patch,
                const std::string &filename) {
  try {
    auto filepath = build_patch_path(patches_dir, filename);

    if (!std::filesystem::exists(filepath)) {
      std::cerr << "File does not exist: " << filepath << std::endl;
      return false;
    }

    std::ifstream file(filepath);
    if (!file) {
      std::cerr << "Failed to open file for reading: " << filepath << std::endl;
      return false;
    }

    nlohmann::json j;
    file >> j;
    patch = j.get<Patch>();

    std::cout << "Loaded patch from: " << filepath << std::endl;
    return true;
  } catch (const std::exception &e) {
    std::cerr << "Load error: " << e.what() << std::endl;
    return false;
  }
}

std::vector<std::string>
list_patch_files(const std::filesystem::path &patches_dir) {
  std::vector<std::string> files;

  if (!std::filesystem::exists(patches_dir)) {
    return files;
  }

  try {
    for (const auto &entry : std::filesystem::directory_iterator(patches_dir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".gin") {
        files.push_back(entry.path().stem().string());
      }
    }
  } catch (const std::filesystem::filesystem_error &e) {
    std::cerr << "Error listing files: " << e.what() << std::endl;
  }

  return files;
}

} // namespace ym2612::formats::gin
