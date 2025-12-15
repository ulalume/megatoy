#include "patch_loader.hpp"

#include "patch_registry.hpp"
#include "dmp.hpp"
#include "rym2612.hpp"

#include <algorithm>
#include <filesystem>
#include <utility>

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
  auto result = PatchRegistry::instance().load(path);
  if (result.status == PatchLoadStatus::Success ||
      result.status == PatchLoadStatus::MultiInstrument) {
    for (auto &patch : result.patches) {
      ensure_patch_name(patch, path);
    }
  }
  return result;
}

std::string get_patch_name_from_file(const std::filesystem::path &path,
                                     const std::string &format) {
  if (format == "rym2612" || format == ".rym2612") {
    return formats::rym2612::get_patch_name(path);
  } else if (format == "dmp" || format == ".dmp") {
    return formats::dmp::get_patch_name(path);
  }
  return path.stem().string();
}

} // namespace formats
