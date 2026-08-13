#pragma once

#include "platform/virtual_file_system.hpp"
#include <filesystem>

namespace megatoy::system {

/**
 * The few paths megatoy owns.
 *
 * Patches no longer live in a directory the application manages -- they live
 * in whatever folders the user adds to the workspace (see
 * megatoy::workspace::Workspace). What remains here is configuration, which
 * belongs to the app, and the read-only presets shipped inside the bundle.
 */
struct DirectoryPaths {
  std::filesystem::path builtin_presets_root;
  std::filesystem::path preferences_file;
  std::filesystem::path imgui_ini_file;
  /// SQLite metadata database written by pre-workspace desktop releases.
  std::filesystem::path legacy_patch_metadata_db;
};

class PathService {
public:
  PathService();
  /**
   * @param config_root Overrides where preferences.json and imgui.ini live.
   *                    Tests pass a temporary directory so they cannot
   *                    overwrite the real user configuration.
   */
  explicit PathService(::platform::VirtualFileSystem &vfs,
                       std::filesystem::path config_root = {});

  const DirectoryPaths &paths() const { return paths_; }
  ::platform::VirtualFileSystem &file_system() { return vfs_; }
  const ::platform::VirtualFileSystem &file_system() const { return vfs_; }

  /// Create the configuration directory. Patch folders belong to the user.
  bool ensure_directories() const;

  static std::filesystem::path executable_directory();
  static std::filesystem::path builtin_presets_directory();
  static std::filesystem::path preferences_file_path();
  static std::filesystem::path imgui_ini_file_path();

  /// Where a save or export dialog starts when nothing better is known.
  static std::filesystem::path default_documents_directory();

  /**
   * Patch folder created automatically by pre-workspace desktop releases.
   * Its existence is the only launch marker for users who never changed a
   * setting (and therefore never got an old preferences.json).
   */
  static std::filesystem::path legacy_default_patches_directory();

  /**
   * Root of the browser's persistent storage (an IDBFS mount backed by
   * IndexedDB). Imported folders and newly saved patches live under here.
   *
   * Empty on desktop, where patches live wherever the user keeps them.
   */
  static std::filesystem::path web_storage_root();

private:
  DirectoryPaths paths_;

  static std::filesystem::path
  canonical_or_normal(const std::filesystem::path &path);
  static std::filesystem::path executable_directory_impl();

  ::platform::VirtualFileSystem &vfs_;
};

} // namespace megatoy::system
