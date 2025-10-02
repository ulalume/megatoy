#include "patch_loader.hpp"

#include "../formats/ctrmml.hpp"
#include "../formats/dmp.hpp"
#include "../formats/gin.hpp"
#include "fui_parser.hpp"
#include "rym2612_parser.hpp"

#include <algorithm>
#include <filesystem>

namespace parsers {

namespace {

std::string lowercase_extension(const std::filesystem::path &path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return ext;
}

void ensure_patch_name(ym2612::Patch &patch,
                       const std::filesystem::path &source) {
  if (patch.name.empty()) {
    patch.name = source.stem().string();
  }
}

} // namespace

PatchLoadResult load_patch_from_file(const std::filesystem::path &path) {
  PatchLoadResult result;

  if (!std::filesystem::exists(path) ||
      !std::filesystem::is_regular_file(path)) {
    result.status = PatchLoadStatus::Failure;
    result.message = "File not found: " + path.string();
    return result;
  }

  const std::string ext = lowercase_extension(path);
  ym2612::Patch patch;

  if (ext == ".gin") {
    if (ym2612::formats::gin::load_patch(path.parent_path(), patch,
                                         path.stem().string())) {
      ensure_patch_name(patch, path);
      result.status = PatchLoadStatus::Success;
      result.patch = patch;
      return result;
    }
    result.message = "Failed to load GIN patch.";
  } else if (ext == ".dmp") {
    if (ym2612::formats::dmp::read_file(path, patch)) {
      ensure_patch_name(patch, path);
      result.status = PatchLoadStatus::Success;
      result.patch = patch;
      return result;
    }
    result.message = "Failed to load DMP patch.";
  } else if (ext == ".rym2612") {
    if (parsers::parse_rym2612_file(path, patch)) {
      ensure_patch_name(patch, path);
      result.status = PatchLoadStatus::Success;
      result.patch = patch;
      return result;
    }
    result.message = "Failed to load RYM2612 patch.";
  } else if (ext == ".fui") {
    if (parsers::parse_fui_file(path, patch)) {
      ensure_patch_name(patch, path);
      result.status = PatchLoadStatus::Success;
      result.patch = patch;
      return result;
    }
    result.message = "Failed to load FUI patch.";
  } else if (ext == ".mml") {
    std::vector<ym2612::formats::ctrmml::Instrument> instruments;
    if (ym2612::formats::ctrmml::read_file(path, instruments)) {
      if (!instruments.empty()) {
        if (instruments.size() == 1) {
          patch = instruments.front().patch;
          if (!instruments.front().name.empty()) {
            patch.name = instruments.front().name;
          }
          ensure_patch_name(patch, path);
          result.status = PatchLoadStatus::Success;
          result.patch = patch;
        } else {
          result.status = PatchLoadStatus::MultiInstrument;
          result.instruments = std::move(instruments);
        }
        return result;
      }
      result.message = "No instruments found in MML file.";
    } else {
      result.message = "Failed to parse MML file.";
    }
  } else {
    result.message = "Unsupported file extension: " + ext;
  }

  return result;
}

} // namespace parsers
