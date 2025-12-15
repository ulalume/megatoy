#pragma once

#include "patch_storage.hpp"
#include "platform/virtual_file_system.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace patches {

class PatchMetadataManager;

class FilesystemPatchStorage final : public PatchStorage {
public:
  FilesystemPatchStorage(platform::VirtualFileSystem &vfs,
                         std::filesystem::path root,
                         std::string relative_root_label,
                         PatchMetadataManager *metadata_manager,
                         bool writable,
                         std::optional<std::filesystem::path> write_root = {});

  void append_entries(std::vector<PatchEntry> &tree) const override;
  bool load_patch(const PatchEntry &entry,
                  ym2612::Patch &out_patch) const override;
  SavePatchResult save_patch(const ym2612::Patch &patch,
                             const std::string &name,
                             bool overwrite) override;
  bool is_writable() const override { return writable_; }
  std::string_view label() const override { return label_; }
  bool save_patch_metadata(const std::string &relative_path,
                           const ym2612::Patch &patch,
                           const PatchMetadata &metadata) override;
  bool update_patch_metadata(const std::string &relative_path,
                             const PatchMetadata &metadata) override;
  std::optional<PatchMetadata>
  get_patch_metadata(const std::string &relative_path) const override;
  void cleanup_metadata(const std::vector<std::string> &paths) const override;
  std::optional<bool>
  has_patch_named(const std::string &name) const override;
  std::optional<std::filesystem::path>
  to_relative_path(const std::filesystem::path &path) const override;
  std::optional<std::filesystem::path>
  to_absolute_path(const std::filesystem::path &path) const override;

private:
  platform::VirtualFileSystem &vfs_;
  std::filesystem::path root_;
  std::string root_label_;
  PatchMetadataManager *metadata_manager_;
  bool writable_;
  std::string label_;
  std::optional<std::filesystem::path> write_root_;

  void scan_directory(const std::filesystem::path &dir_path,
                      std::vector<PatchEntry> &tree,
                      const std::string &relative_path = "") const;
  static std::string detect_format(const std::filesystem::path &file_path);
  static bool is_supported_file(const std::filesystem::path &file_path);
  void load_metadata_for_entry(PatchEntry &entry) const;
};

} // namespace patches
