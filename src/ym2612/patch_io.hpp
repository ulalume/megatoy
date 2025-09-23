#pragma once

#include "patch.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace ym2612 {

std::filesystem::path build_patch_path(const std::filesystem::path &patches_dir,
                                       const std::string &filename);

const std::optional<std::filesystem::path>
save_patch(const std::filesystem::path &patches_dir, const Patch &patch,
           const std::string &filename);

bool load_patch(const std::filesystem::path &patches_dir, Patch &patch,
                const std::string &filename);

std::vector<std::string>
list_patch_files(const std::filesystem::path &patches_dir);

} // namespace ym2612
