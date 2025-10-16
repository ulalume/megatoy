#pragma once

#include "../ym2612/patch.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace formats::ctrmml {

std::vector<ym2612::Patch> read_file(const std::filesystem::path &file_path);

std::string patch_to_string(const ym2612::Patch &patch);

bool write_patch(const ym2612::Patch &patch,
                 const std::filesystem::path &target_path);

} // namespace formats::ctrmml
