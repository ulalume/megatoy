#pragma once

#include "folder_watcher.hpp"
#include "patches/patch_metadata.hpp"
#include "platform/std_file_system.hpp"
#include "save_patch_result.hpp"
#include "workspace/workspace.hpp"
#include "ym2612/patch.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace patches {

class FilesystemPatchStorage;
class PersistentParseCache;

struct PatchEntry {
  std::string name;
  std::string relative_path;
  std::filesystem::path full_path;
  std::string format;
  bool is_directory = false;
  /// A directory with nothing on disk but dotfiles.
  bool holds_nothing = false;
  /// For a workspace folder sharing its name with another: the folders above
  /// it that tell them apart, as "songs/".
  std::string telling_parents;
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

/// A patch file or a folder as it stood before it was deleted, to put back.
struct DeletedEntry {
  struct File {
    /// Below the folder; empty for the file itself.
    std::filesystem::path relative;
    std::string bytes;
  };
  std::filesystem::path path;
  bool is_directory = false;
  /// A folder's folders, its empty ones among them, below it.
  std::vector<std::filesystem::path> folders;
  std::vector<File> files;
  /// A file's own metadata. A folder's is among its files.
  std::optional<nlohmann::json> metadata;
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
  PatchRepository(platform::StdFileSystem &vfs,
                  const megatoy::workspace::Workspace &workspace,
                  const std::filesystem::path &builtin_presets_dir = {},
                  const std::filesystem::path &builtin_presets_metadata = {},
                  PersistentParseCache *persistent_cache = nullptr);
  ~PatchRepository();

  /// Rebuild the storage list if the workspace has changed since last call.
  /// Returns true when something changed.
  bool sync_workspace();

  void refresh();
  const std::vector<PatchEntry> &tree() const;
  std::uint64_t revision() const { return revision_; }

  /// Exact selection path first, then the first entry from that source.
  const PatchEntry *find_entry(const std::string &relative_path) const;

  bool load_patch(const PatchEntry &entry, ym2612::Patch &patch) const;
  bool has_directory_changed() const;
  /// True once something in the workspace's folders has changed outside
  /// megatoy and settled since this last returned true.
  bool take_outside_change();

  static std::vector<std::string> supported_extensions();

  std::filesystem::path
  to_relative_path(const std::filesystem::path &path) const;
  std::filesystem::path
  to_absolute_path(const std::filesystem::path &path) const;

  // Metadata operations
  bool update_patch_metadata(const std::string &relative_path,
                             const PatchMetadata &metadata);

  SavePatchResult save_patch(const ym2612::Patch &patch,
                             const std::string &name, bool overwrite,
                             std::string_view preferred_extension);

  /// Save into one specific workspace folder rather than the default one.
  SavePatchResult save_patch_in(const std::filesystem::path &folder,
                                const ym2612::Patch &patch,
                                const std::string &name, bool overwrite,
                                std::string_view preferred_extension);

  bool can_delete_patch(const PatchEntry &entry) const;
  bool can_rename_patch(const PatchEntry &entry) const;
  bool delete_patch(const PatchEntry &entry);
  bool rename_patch(const PatchEntry &entry, const std::string &new_stem);
  /// Moves stay inside one workspace folder: the storage that owns `entry`
  /// has to own `destination` too.
  bool can_move_patch(const PatchEntry &entry,
                      const PatchEntry &destination) const;
  bool move_patch(const PatchEntry &entry, const PatchEntry &destination);
  /// Copies go into a plain folder of a writable workspace folder, from
  /// anywhere: another workspace folder, the built-in presets or a bank.
  bool takes_copies(const PatchEntry &destination) const;
  bool can_copy_patch(const PatchEntry &entry,
                      const PatchEntry &destination) const;
  /**
   * Copies `entry` into `destination` and returns the copy's path. The copy
   * keeps the entry's name, or takes "<name> copy", "<name> copy 2", ... when
   * that is taken. A patch from inside a bank or a package becomes a file of
   * its own in `extension`.
   */
  std::optional<std::filesystem::path> copy_patch(const PatchEntry &entry,
                                                  const PatchEntry &destination,
                                                  std::string_view extension);
  /// Copies files and folders from outside into `destination`, as
  /// copy_patch names them, and returns the copies. A file no format reads
  /// is left out, counted in `skipped` with those that fail.
  std::vector<std::filesystem::path>
  copy_files_in(const std::vector<std::filesystem::path> &files,
                const PatchEntry &destination, std::size_t &skipped);
  /// Removes a copy made by copy_patch, file or folder, with its metadata.
  bool remove_copy(const std::filesystem::path &copy);
  /// What `entry` holds, read before deleting it: the file and its metadata,
  /// or the folder and everything in it. Nothing for a folder holding more
  /// than `limit` bytes.
  std::optional<DeletedEntry>
  keep_for_restore(const PatchEntry &entry,
                   std::uintmax_t limit = 64u << 20) const;
  /// Writes a deleted entry back where it was, if nothing has taken its place.
  bool restore(const DeletedEntry &deleted);
  /// A relative path as the user sees it: starting with what its folder is
  /// listed as.
  std::string display_path(const std::string &relative) const;
  bool can_edit_metadata(const PatchEntry &entry) const;

  /// Give `file`, a patch megatoy just wrote as a copy, the metadata of the
  /// one it copies, without its hash and timestamps. Nothing outside the
  /// writable folders.
  void copy_patch_metadata(const std::optional<PatchMetadata> &source,
                           const std::filesystem::path &file);

  /// The folders the tree mirrors, in the order it lists them.
  const megatoy::workspace::Workspace &workspace() const { return workspace_; }

  bool show_builtin_presets() const { return show_builtin_presets_; }
  void set_show_builtin_presets(bool show);

private:
  static constexpr const char *kBuiltinRootName = "presets";

  void rebuild_storages();
  void index_tree();

  const megatoy::workspace::Workspace &workspace_;
  std::filesystem::path builtin_presets_directory_;
  std::filesystem::path builtin_presets_metadata_;
  platform::StdFileSystem &vfs_;
  PersistentParseCache *persistent_cache_;

  std::vector<PatchEntry> tree_cache_;
  /// tree_cache_'s patches by relative_path and by source_relative_path,
  /// the first in tree order kept for each.
  std::unordered_map<std::string, const PatchEntry *> by_path_;
  std::unordered_map<std::string, const PatchEntry *> by_source_path_;
  std::vector<std::filesystem::path> watched_directories_;
  FolderWatcher folder_watcher_;
  std::vector<std::filesystem::file_time_type> watched_times_;
  bool cache_initialized_ = false;
  bool show_builtin_presets_ = true;
  std::uint64_t synced_revision_ = 0;
  bool storages_built_ = false;
  std::uint64_t revision_ = 0;

  std::vector<std::unique_ptr<FilesystemPatchStorage>> storages_;
};

} // namespace patches
