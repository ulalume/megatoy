#pragma once

#include "../ym2612/patch.hpp"
#include <filesystem>
#include <string>

namespace ym2612::formats::dmp {

bool read_file(const std::filesystem::path &file_path, ym2612::Patch &patch);

bool write_patch(const ym2612::Patch &patch,
                 const std::filesystem::path &target_path);

std::string get_patch_name(const std::filesystem::path &file_path);

} // namespace ym2612::formats::dmp
