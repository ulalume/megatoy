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
  // Optional writers for export/save; not all formats support these.
  std::function<std::optional<std::filesystem::path>(
      const std::filesystem::path &, const ym2612::Patch &, const std::string &)>
      write_packaged; // writes to directory + name
  std::function<bool(const ym2612::Patch &,
                     const std::filesystem::path &)>
      write_single; // writes to explicit path
  std::function<bool(const ym2612::Patch &,
                     const std::filesystem::path &)> write_text;
  // Label to present in UI.
  std::string label;
};

struct ExportFormatInfo {
  std::string extension;
  std::string label;
  bool is_text = false;
};

class PatchRegistry {
public:
  static PatchRegistry &instance();

  void register_format(const std::string &extension,
                       PatchFormatHandler handler);

  const PatchFormatHandler *
  handler_for_extension(const std::string &extension) const;

  PatchLoadResult load(const std::filesystem::path &path) const;
  // Optional single-patch writer by extension.
  bool write(const std::string &extension, const ym2612::Patch &patch,
             const std::filesystem::path &target) const;
  bool write_text(const std::string &extension, const ym2612::Patch &patch,
                  const std::filesystem::path &target) const;
  std::optional<std::filesystem::path>
  save_package(const std::string &extension, const std::filesystem::path &dir,
               const std::string &name, const ym2612::Patch &patch) const;
  std::vector<ExportFormatInfo> export_formats() const;

private:
  PatchRegistry();
  void register_defaults();

  std::unordered_map<std::string, PatchFormatHandler> handlers_;
};

} // namespace formats
