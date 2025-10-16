#include "patch_loader.hpp"

#include "ctrmml.hpp"
#include "dmp.hpp"
#include "fui.hpp"
#include "gin.hpp"
#include "rym2612.hpp"

#include <algorithm>
#include <filesystem>

namespace formats {

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
    patch.name = get_patch_name_from_file(source, lowercase_extension(source));
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

  using ReaderFunc =
      std::function<std::vector<ym2612::Patch>(const std::filesystem::path &)>;
  static const std::unordered_map<std::string,
                                  std::pair<ReaderFunc, std::string>>
      readers = {{".gin", {formats::gin::read_file, "GIN"}},
                 {".dmp", {formats::dmp::read_file, "DMP"}},
                 {".rym2612", {formats::rym2612::read_file, "RYM2612"}},
                 {".fui", {formats::fui::read_file, "FUI"}},
                 {".mml", {formats::ctrmml::read_file, "MML"}}};
  if (auto it = readers.find(ext); it != readers.end()) {
    const auto &[reader, label] = it->second;
    auto patches = reader(path);
    const auto patches_size = patches.size();
    if (patches_size == 0) {
      result.status = PatchLoadStatus::Failure;
      if (label == "MML") {
        result.message = "No instruments found in MML file.";
      } else {
        result.message = "Failed to load " + label + " patch.";
      }
    } else if (patches_size == 1) {
      ensure_patch_name(patches[0], path);
      result.status = PatchLoadStatus::Success;
      result.patches = patches;
    } else {
      for (auto &patch : patches) {
        ensure_patch_name(patch, path);
      }
      result.status = PatchLoadStatus::MultiInstrument;
      result.patches = patches;
    }
  } else {
    result.message = "Unsupported file extension: " + ext;
  }
  return result;
}

std::string get_patch_name_from_file(const std::filesystem::path &path,
                                     const std::string &format) {
  if (format == "rym2612") {
    return formats::rym2612::get_patch_name(path);
  } else if (format == "dmp") {
    return formats::dmp::get_patch_name(path);
  }
  return path.stem().string();
}

} // namespace formats
