#pragma once

#include "patch_loader.hpp"
#include "../ym2612/patch.hpp"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace formats {

struct PatchFormatHandler {
  std::function<std::vector<ym2612::Patch>(const std::filesystem::path &)>
      read_file;
  // Optional writer for export/save; not all formats support saving.
  std::function<bool(const ym2612::Patch &,
                     const std::filesystem::path &)> write_single;
  // Label to present in UI.
  std::string label;
};

class PatchRegistry {
public:
  static PatchRegistry &instance();

  void register_format(const std::string &extension,
                       PatchFormatHandler handler);

  const PatchFormatHandler *
  handler_for_extension(const std::string &extension) const;

  PatchLoadResult load(const std::filesystem::path &path) const;

private:
  PatchRegistry();
  void register_defaults();

  std::unordered_map<std::string, PatchFormatHandler> handlers_;
};

} // namespace formats
