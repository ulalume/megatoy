#pragma once

#include "../ym2612/patch.hpp"
#include <filesystem>
#include <vector>

namespace parsers {

struct CtrmmlInstrument {
  int instrument_number;
  ym2612::Patch patch;
  std::string name;
};

bool parse_ctrmml_file(const std::filesystem::path &file_path,
                       std::vector<CtrmmlInstrument> &out_instruments);

} // namespace parsers

