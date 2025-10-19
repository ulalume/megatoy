#pragma once

#include "../ym2612/patch.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace formats {

enum class PatchLoadStatus {
  Success,
  MultiInstrument,
  Failure,
};

struct PatchLoadResult {
  PatchLoadStatus status = PatchLoadStatus::Failure;
  std::vector<ym2612::Patch> patches;
  std::string message;
};

PatchLoadResult load_patch_from_file(const std::filesystem::path &path);
std::string get_patch_name_from_file(const std::filesystem::path &path,
                                     const std::string &format);

} // namespace formats
