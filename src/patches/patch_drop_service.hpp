#pragma once

#include "formats/ctrmml.hpp"
#include "ym2612/patch.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace patches {

struct PatchDropResult {
  enum class Status {
    Loaded,
    MultiInstrument,
    Error,
  };

  Status status = Status::Error;
  std::string history_label;
  ym2612::Patch patch;
  std::filesystem::path source_path;
  std::vector<ym2612::formats::ctrmml::Instrument> instruments;
  std::string error_message;
};

PatchDropResult load_patch_from_path(const std::filesystem::path &path);

} // namespace patches
