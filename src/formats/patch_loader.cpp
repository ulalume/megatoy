#include "patch_loader.hpp"

#include "patch_registry.hpp"

#include <filesystem>

namespace formats {

PatchLoadResult load_patch_from_file(const std::filesystem::path &path) {
  return PatchRegistry::instance().load(path);
}

std::string get_patch_name_from_file(const std::filesystem::path &path,
                                     const std::string &) {
  // Formats that carry a name of their own (.rym2612, .gin, .fui, ...) fill
  // it in while parsing; this is only the fallback for the ones that do not.
  return path.stem().string();
}

} // namespace formats
