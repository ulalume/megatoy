#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace megatoy::workspace {

/// A folder the user has added to the workspace.
struct Folder {
  std::filesystem::path path;
  std::string name; ///< Display name; defaults to the directory name.
  /// False when the directory is gone -- an unplugged drive, a renamed
  /// folder. Such folders stay in the list so the user does not lose their
  /// workspace over a temporary absence.
  bool available = false;
  bool writable = false;

  /// Where this folder's sidecar metadata lives.
  std::filesystem::path metadata_path() const;
};

/**
 * The set of patch folders the user is working with, in the order they should
 * appear in the browser.
 *
 * This replaces megatoy's old single ~/Documents/megatoy tree: folders live
 * wherever the user keeps them, several can be open at once, and the list
 * persists across sessions.
 *
 * The built-in presets are deliberately *not* part of this list. They ship
 * inside the application bundle and are replaced on update, so a stored path
 * to them would go stale as soon as the app moved.
 */
class Workspace {
public:
  const std::vector<Folder> &folders() const { return folders_; }
  bool empty() const { return folders_.empty(); }

  /**
   * Add a folder. Returns false if the path is not a directory or is already
   * present -- comparison is on the resolved path, so the same folder reached
   * by different routes is not added twice.
   */
  bool add(const std::filesystem::path &path);

  bool remove(const std::filesystem::path &path);

  /// Move the folder at `from` to index `to`, keeping the rest in order.
  bool reorder(std::size_t from, std::size_t to);

  bool contains(const std::filesystem::path &path) const;

  /// The folder that owns `path`, if any.
  const Folder *owner_of(const std::filesystem::path &path) const;

  /**
   * Where a brand new patch is saved: the first writable folder. Returns
   * nullopt when the workspace is empty or entirely read-only, in which case
   * the caller should ask the user.
   */
  std::optional<std::filesystem::path> default_save_folder() const;

  /**
   * Replace the whole list, e.g. when loading preferences. Unlike add(),
   * folders that are currently missing are kept and flagged unavailable.
   */
  void set_paths(const std::vector<std::filesystem::path> &paths);
  std::vector<std::filesystem::path> paths() const;

  /// Re-check which folders still exist and are writable.
  void refresh();

  /// Bumped whenever the folder set changes, so observers can rebuild.
  std::uint64_t revision() const { return revision_; }

private:
  static std::filesystem::path normalize(const std::filesystem::path &path);
  static bool probe_writable(const std::filesystem::path &path);

  std::vector<Folder> folders_;
  std::uint64_t revision_ = 0;
};

} // namespace megatoy::workspace
