#pragma once

#include "../ym2612/patch.hpp"
#include <filesystem>

namespace formats::fui {

std::vector<ym2612::Patch> read_file(const std::filesystem::path &file_path);

} // namespace formats::fui
