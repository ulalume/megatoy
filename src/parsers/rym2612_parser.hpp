#pragma once

#include "ym2612/patch.hpp"
#include <filesystem>
#include <string>

namespace parsers {

// Convert a rym2612 file into a Patch
bool parse_rym2612_file(const std::filesystem::path &file_path,
                        ym2612::Patch &patch);

// Retrieve the patch name from a rym2612 file (preview before loading)
std::string get_rym2612_patch_name(const std::filesystem::path &file_path);

} // namespace parsers
