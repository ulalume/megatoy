#pragma once

#include "ym2612/patch.hpp"
#include <filesystem>
#include <optional>
#include <string>

namespace ym2612::formats::gin {

std::filesystem::path build_patch_path(const std::filesystem::path &patches_dir,
                                       const std::string &filename);

const std::optional<std::filesystem::path>
save_patch(const std::filesystem::path &patches_dir, const Patch &patch,
           const std::string &filename);

bool load_patch(const std::filesystem::path &patches_dir, Patch &patch,
                const std::string &filename);

} // namespace ym2612::formats::gin
