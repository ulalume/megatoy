#pragma once

#include "../ym2612/patch.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace ym2612::formats::ctrmml {

struct Instrument {
  int instrument_number;
  ym2612::Patch patch;
  std::string name;
};

bool read_file(const std::filesystem::path &file_path,
               std::vector<Instrument> &out_instruments);

bool write_patch(const ym2612::Patch &patch,
                 const std::filesystem::path &target_path);

} // namespace ym2612::formats::ctrmml
