#pragma once

#include "../ym2612/patch.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace formats::dmp {

std::vector<ym2612::Patch> read_file(const std::filesystem::path &file_path);

std::vector<uint8_t> serialize_patch(const ym2612::Patch &patch);

bool write_patch(const ym2612::Patch &patch,
                 const std::filesystem::path &target_path);

std::string get_patch_name(const std::filesystem::path &file_path);

} // namespace formats::dmp
