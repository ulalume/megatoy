#pragma once

#include "patch_storage.hpp"
#include "patches/folder_metadata.hpp"
#include "platform/virtual_file_system.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace patches {

/**
 * One workspace folder (or the built-in presets directory) presented as a
 * patch source.
 *
 * `root_label` is the name the folder appears under in the browser and the
 * prefix of every relative path it produces, so it has to be unique across
 * the storages a repository holds.
 *
 * Metadata lives in a sidecar inside the folder itself and is therefore keyed
 * by a path relative to `root`, not to the repository -- that is what lets a
 * folder be moved or shared without losing its ratings. Read-only folders
 * (the presets directory, a mounted archive) get no store at all.
 */
class FilesystemPatchStorage final : public PatchStorage {
public:
  FilesystemPatchStorage(platform::VirtualFileSystem &vfs,
                         std::filesystem::path root,
                         std::string relative_root_label, bool writable,
                         bool enable_metadata);

  void append_entries(std::vector<PatchEntry> &tree) const override;
  bool load_patch(const PatchEntry &entry,
                  ym2612::Patch &out_patch) const override;
  SavePatchResult save_patch(const ym2612::Patch &patch,
                             const std::string &name, bool overwrite,
                             std::string_view preferred_extension) override;
  bool can_delete_patch(const PatchEntry &entry) const override;
  bool delete_patch(const PatchEntry &entry) override;
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
  std::optional<bool> has_patch_named(const std::string &name) const override;
  std::optional<std::filesystem::path>
  to_relative_path(const std::filesystem::path &path) const override;
  std::optional<std::filesystem::path>
  to_absolute_path(const std::filesystem::path &path) const override;

  const std::filesystem::path &root() const { return root_; }

private:
  platform::VirtualFileSystem &vfs_;
  std::filesystem::path root_;
  std::string root_label_;
  bool writable_;
  std::string label_;
  std::unique_ptr<FolderMetadataStore> metadata_;

  void scan_directory(const std::filesystem::path &dir_path,
                      std::vector<PatchEntry> &tree,
                      const std::string &relative_path = "") const;
  static std::string detect_format(const std::filesystem::path &file_path);
  static bool is_supported_file(const std::filesystem::path &file_path);
  void load_metadata_for_entry(PatchEntry &entry) const;

  /// Strip the root label so metadata keys stay relative to the folder.
  std::string metadata_key(const std::string &relative_path) const;
  bool owns_relative_path(const std::string &relative_path) const;
};

} // namespace patches
