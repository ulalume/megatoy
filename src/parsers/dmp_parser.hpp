#pragma once

#include "../ym2612/patch.hpp"
#include <filesystem>
#include <string>

namespace parsers {

// Convert a DMP file into a Patch
bool parse_dmp_file(const std::filesystem::path &file_path,
                    ym2612::Patch &patch);

// Retrieve the patch name from a DMP file (preview before loading)
std::string get_dmp_patch_name(const std::filesystem::path &file_path);

} // namespace parsers
