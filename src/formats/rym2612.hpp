#pragma once

#include "../ym2612/patch.hpp"
#include <filesystem>
#include <string>

namespace formats::rym2612 {

// Convert a rym2612 file into a Patch
std::vector<ym2612::Patch> read_file(const std::filesystem::path &file_path);

// Retrieve the patch name from a rym2612 file (preview before loading)
std::string get_patch_name(const std::filesystem::path &file_path);

} // namespace formats::rym2612
