#pragma once

#include "../ym2612/patch.hpp"
#include <filesystem>

namespace parsers {

// Parse Furnace instrument (FUI) files for FM (OPN) instruments.
// Returns true on success and fills the provided patch structure.
bool parse_fui_file(const std::filesystem::path &file_path,
                    ym2612::Patch &patch);

} // namespace parsers
