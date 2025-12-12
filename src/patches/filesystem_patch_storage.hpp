#pragma once

#include "patch_storage.hpp"
#include "platform/virtual_file_system.hpp"
#include <filesystem>
#include <memory>
#include <string>

namespace patches {

class PatchMetadataManager;

class FilesystemPatchStorage final : public PatchStorage {
public:
  FilesystemPatchStorage(platform::VirtualFileSystem &vfs,
                         std::filesystem::path root,
                         std::string relative_root_label,
                         PatchMetadataManager *metadata_manager);

  void append_entries(std::vector<PatchEntry> &tree) const override;
  bool load_patch(const PatchEntry &entry,
                  ym2612::Patch &out_patch) const override;

private:
  platform::VirtualFileSystem &vfs_;
  std::filesystem::path root_;
  std::string root_label_;
  PatchMetadataManager *metadata_manager_;

  void scan_directory(const std::filesystem::path &dir_path,
                      std::vector<PatchEntry> &tree,
                      const std::string &relative_path = "") const;
  static std::string detect_format(const std::filesystem::path &file_path);
  static bool is_supported_file(const std::filesystem::path &file_path);
  void load_metadata_for_entry(PatchEntry &entry) const;
};

} // namespace patches
