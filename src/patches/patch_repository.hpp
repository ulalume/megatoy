#pragma once

#include "patch_metadata.hpp"
#include "platform/virtual_file_system.hpp"
#include "ym2612/patch.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace patches {

struct PatchEntry {
  std::string name;
  std::string relative_path;
  std::filesystem::path full_path;
  std::string format;
  bool is_directory;
  std::vector<PatchEntry> children;

  // Metadata (only valid for files, not directories)
  std::optional<PatchMetadata> metadata;

  // For ctrmml files: instrument index within the file
  size_t ctrmml_index = 0;
};

class PatchRepository {
public:
  PatchRepository(platform::VirtualFileSystem &vfs,
                  const std::filesystem::path &patches_root,
                  const std::filesystem::path &builtin_dir = {},
                  const std::filesystem::path &metadata_db_path = {});

  void refresh();
  const std::vector<PatchEntry> &tree() const;

  bool load_patch(const PatchEntry &entry, ym2612::Patch &patch) const;
  bool has_directory_changed() const;

  static std::vector<std::string> supported_extensions();

  std::filesystem::path
  to_relative_path(const std::filesystem::path &path) const;
  std::filesystem::path
  to_absolute_path(const std::filesystem::path &path) const;

  // Metadata operations
  bool save_patch_metadata(const std::string &relative_path,
                           const ym2612::Patch &patch,
                           const PatchMetadata &metadata);
  bool update_patch_metadata(const std::string &relative_path,
                             const PatchMetadata &metadata);
  std::optional<PatchMetadata>
  get_patch_metadata(const std::string &relative_path) const;

  // Batch operations
  std::vector<PatchEntry> get_patches_by_metadata_filter(
      const std::function<bool(const PatchMetadata &)> &filter) const;
  void cleanup_orphaned_metadata();

private:
  static constexpr const char *kBuiltinRootName = "presets";

  std::filesystem::path patches_directory_;
  std::filesystem::path builtin_patch_directory_;
  platform::VirtualFileSystem &vfs_;
  std::unique_ptr<PatchMetadataManager> metadata_manager_;

  std::vector<PatchEntry> tree_cache_;
  std::filesystem::file_time_type last_user_directory_check_time_{};
  std::filesystem::file_time_type last_builtin_directory_check_time_{};
  bool has_builtin_directory_ = false;
  bool user_time_valid_ = false;
  bool builtin_time_valid_ = false;
  bool cache_initialized_;

  void scan_directory(const std::filesystem::path &dir_path,
                      std::vector<PatchEntry> &tree,
                      const std::string &relative_path = "");
  void load_metadata_for_entry(PatchEntry &entry);
  std::string detect_format(const std::filesystem::path &file_path) const;
  bool is_supported_file(const std::filesystem::path &file_path) const;
};

} // namespace patches
