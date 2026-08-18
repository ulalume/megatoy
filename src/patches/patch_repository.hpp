#pragma once

#include "patch_storage.hpp"
#include "patches/folder_metadata.hpp"
#include "platform/virtual_file_system.hpp"
#include "workspace/workspace.hpp"
#include "ym2612/patch.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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

  // For bank formats (.mml, .dmf, .fur, .opm): which instrument in the file
  size_t instrument_index = 0;

  // GINPKG versions are presented like instruments in other container files.
  // The source path keeps Save Version and file-level actions attached to the
  // parent package while container_item_id identifies the selected snapshot.
  std::string source_relative_path;
  std::string container_item_id;
};

/**
 * Presents the workspace's folders, plus the built-in presets, as one tree of
 * patches.
 *
 * The repository owns no directories of its own: it mirrors whatever folders
 * the user has added. When the workspace changes, sync_workspace() rebuilds
 * the storage list.
 */
class PatchRepository {
public:
  PatchRepository(platform::VirtualFileSystem &vfs,
                  const megatoy::workspace::Workspace &workspace,
                  const std::filesystem::path &builtin_presets_dir = {});

  /// Rebuild the storage list if the workspace has changed since last call.
  /// Returns true when something changed.
  bool sync_workspace();

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

  SavePatchResult save_patch(const ym2612::Patch &patch,
                             const std::string &name, bool overwrite,
                             std::string_view preferred_extension);

  /// Save into one specific workspace folder rather than the default one.
  SavePatchResult save_patch_in(const std::filesystem::path &folder,
                                const ym2612::Patch &patch,
                                const std::string &name, bool overwrite,
                                std::string_view preferred_extension);

  bool can_delete_patch(const PatchEntry &entry) const;
  bool delete_patch(const PatchEntry &entry);

  // Batch operations
  std::vector<PatchEntry> get_patches_by_metadata_filter(
      const std::function<bool(const PatchMetadata &)> &filter) const;
  void cleanup_orphaned_metadata();

  // Capabilities
  std::string primary_writable_label() const;
  bool patch_name_conflicts(const std::string &name) const;

  bool show_builtin_presets() const { return show_builtin_presets_; }
  void set_show_builtin_presets(bool show);

private:
  static constexpr const char *kBuiltinRootName = "presets";

  void rebuild_storages();

  const megatoy::workspace::Workspace &workspace_;
  std::filesystem::path builtin_presets_directory_;
  platform::VirtualFileSystem &vfs_;

  std::vector<PatchEntry> tree_cache_;
  std::vector<std::filesystem::path> watched_directories_;
  std::vector<std::filesystem::file_time_type> watched_times_;
  bool cache_initialized_ = false;
  bool show_builtin_presets_ = true;
  std::uint64_t synced_revision_ = 0;
  bool storages_built_ = false;

  std::vector<std::unique_ptr<PatchStorage>> storages_;
};

} // namespace patches
