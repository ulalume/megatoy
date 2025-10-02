#pragma once

#include "../formats/ctrmml.hpp"
#include "../ym2612/patch.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace parsers {

enum class PatchLoadStatus {
  Success,
  MultiInstrument,
  Failure,
};

struct PatchLoadResult {
  PatchLoadStatus status = PatchLoadStatus::Failure;
  ym2612::Patch patch;
  std::vector<ym2612::formats::ctrmml::Instrument> instruments;
  std::string message;
};

PatchLoadResult load_patch_from_file(const std::filesystem::path &path);

} // namespace parsers
