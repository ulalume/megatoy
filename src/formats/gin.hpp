#pragma once

#include "../ym2612/patch.hpp"
#include <filesystem>
#include <optional>
#include <string>

namespace formats::gin {

std::filesystem::path build_patch_path(const std::filesystem::path &patches_dir,
                                       const std::string &filename);

const std::optional<std::filesystem::path>
save_patch(const std::filesystem::path &patches_dir, const ym2612::Patch &patch,
           const std::string &filename);

std::vector<ym2612::Patch> read_file(const std::filesystem::path &file_path);

} // namespace formats::gin
