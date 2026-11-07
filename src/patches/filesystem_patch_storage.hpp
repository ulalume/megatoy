#pragma once

#include "patch_repository.hpp"
#include "patches/patch_metadata.hpp"
#include "platform/std_file_system.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace patches {

class PersistentParseCache;

/**
 * One workspace folder (or the built-in presets directory) presented as a
 * patch source.
 *
 * `root_label` is the name the folder appears under in the browser and the
 * prefix of every relative path it produces, so it has to be unique across
 * the storages a repository holds.
 *
 * Each patch's metadata is read from its sidecar (see patch_metadata.hpp)
 * and, in a writable folder, written there. A read-only folder can take the
 * user's own metadata in `metadata_overlay_file` instead, laid over its
 * sidecars (see MetadataOverlay).
 */
class FilesystemPatchStorage {
public:
  FilesystemPatchStorage(platform::StdFileSystem &vfs,
                         std::filesystem::path root,
                         std::string relative_root_label, bool writable,
                         PersistentParseCache *persistent_cache = nullptr,
                         std::filesystem::path metadata_overlay_file = {});

  void append_entries(std::vector<PatchEntry> &tree) const;
  bool load_patch(const PatchEntry &entry, ym2612::Patch &out_patch) const;
  SavePatchResult save_patch(const ym2612::Patch &patch,
                             const std::string &name, bool overwrite,
                             std::string_view preferred_extension);
  bool can_delete_patch(const PatchEntry &entry) const;
  bool can_rename_patch(const PatchEntry &entry) const;
  bool delete_patch(const PatchEntry &entry);
  bool rename_patch(const PatchEntry &entry, const std::string &new_stem);
  bool can_move_patch(const PatchEntry &entry,
                      const PatchEntry &destination) const;
  bool move_patch(const PatchEntry &entry, const PatchEntry &destination);
  bool can_edit_metadata(const PatchEntry &entry) const;
  /// What the folder itself is listed as, in place of its label, and the
  /// folders above it to show beside that.
  void set_listed_as(std::string name, std::string parents) {
    listed_name_ = std::move(name);
    listed_parents_ = std::move(parents);
  }
  bool update_patch_metadata(const std::string &relative_path,
                             const PatchMetadata &metadata);
  /// Give `file`, a patch just written, the metadata `source` without its
  /// hash and timestamps. False when `file` is not in this writable folder.
  bool copy_metadata_to(const std::optional<PatchMetadata> &source,
                        const std::filesystem::path &file);
  std::optional<std::filesystem::path>
  to_relative_path(const std::filesystem::path &path) const;
  std::optional<std::filesystem::path>
  to_absolute_path(const std::filesystem::path &path) const;

  const std::filesystem::path &root() const { return root_; }
  std::size_t container_parse_count_for_testing() const {
    return container_parse_count_;
  }

  /**
   * Hook for a walk that has to report progress or stop early.
   *
   * `on_file` runs once per regular file, before that file is classified or
   * parsed, and returning false abandons the rest of the walk. Unset by
   * default, which is exactly the plain scan.
   */
  struct ScanObserver {
    std::function<bool(const std::filesystem::path &)> on_file;
  };
  void set_scan_observer(ScanObserver observer);

private:
  struct ParseCacheEntry {
    std::uintmax_t file_size = 0;
    std::filesystem::file_time_type modified;
    PatchEntry subtree;
  };

  platform::StdFileSystem &vfs_;
  std::filesystem::path root_;
  std::string root_label_;
  bool writable_;
  std::string label_;
  std::string listed_name_;
  std::string listed_parents_;
  std::unique_ptr<MetadataOverlay> overlay_;
  PersistentParseCache *persistent_cache_;
  mutable std::unordered_map<std::filesystem::path, ParseCacheEntry>
      parse_cache_;
  mutable std::unordered_set<std::filesystem::path> seen_container_paths_;
  mutable std::size_t container_parse_count_ = 0;
  ScanObserver scan_observer_;
  mutable bool scan_aborted_ = false;

  /// Appends what `dir_path` lists, and returns true when it holds nothing
  /// but dotfiles.
  bool scan_directory(const std::filesystem::path &dir_path,
                      std::vector<PatchEntry> &tree,
                      const std::string &relative_path = "") const;
  static std::string detect_format(const std::filesystem::path &file_path);
  static bool is_supported_file(const std::filesystem::path &file_path);
  /// `sidecars_here` is whether the entry's folder has a sidecar folder.
  void load_metadata_for_entry(PatchEntry &entry, bool sidecars_here) const;
  void load_metadata_for_subtree(PatchEntry &entry, bool sidecars_here) const;
  /// What the patch at `relative_path` has: its sidecar, under the overlay.
  std::optional<nlohmann::json>
  read_metadata(const std::filesystem::path &sidecar,
                const std::string &relative_path) const;

  /// Strip the root label so overlay keys stay relative to the folder.
  std::string metadata_key(const std::string &relative_path) const;
  bool owns_relative_path(const std::string &relative_path) const;
  /// The entry's path on disk is the one its relative path maps to.
  bool maps_to_own_path(const PatchEntry &entry) const;
};

} // namespace patches
