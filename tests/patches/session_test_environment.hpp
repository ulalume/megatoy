#pragma once

#include "../test_check.hpp"
#include "audio/audio_manager.hpp"
#include "patches/patch_session.hpp"
#include "patches/patch_write.hpp"
#include "platform/std_file_system.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace session_tests {

namespace fs = std::filesystem;

inline const patches::PatchEntry *
find_patch_entry(const std::vector<patches::PatchEntry> &entries,
                 const fs::path &path) {
  for (const auto &entry : entries) {
    if (entry.full_path == path) {
      return &entry;
    }
    if (const auto *found = find_patch_entry(entry.children, path)) {
      return found;
    }
  }
  return nullptr;
}

// Everything lives under a temporary root, including the configuration, so a
// test run can never touch the developer's real preferences or patches.
struct Environment {
  fs::path root;
  fs::path folder;
  platform::StdFileSystem file_system;
  megatoy::system::PathService directories;
  PreferenceManager preferences;
  AudioManager audio;
  patches::PatchSession session;

  static fs::path make_root(const std::string &name) {
    auto root = fs::temp_directory_path() / ("megatoy_" + name);
    fs::remove_all(root);
    fs::create_directories(root / "patches");
    // The workspace resolves symlinks, and on macOS the temp directory is one;
    // resolve here too so path comparisons line up.
    return fs::weakly_canonical(root);
  }

  explicit Environment(const std::string &name, bool read_only = false)
      : root(make_root(name)), folder(root / "patches"),
        directories(file_system, root / "config"), preferences(directories),
        session(directories, preferences, audio) {
    CHECK(directories.ensure_directories());
    CHECK(preferences.add_workspace_folder(folder));
    if (read_only) {
      set_read_only();
    }
    session.sync_workspace();
    session.initialize_patch_defaults();
  }

  ~Environment() { fs::remove_all(root); }

  patches::PatchEntry entry(const fs::path &path, bool directory = false) {
    return {.name = path.stem().string(),
            .relative_path =
                session.repository().to_relative_path(path).generic_string(),
            .full_path = path,
            .is_directory = directory};
  }

  patches::PatchDocument &open(const fs::path &path) {
    const auto id = session.open_document({}, path);
    return *session.find_document(id);
  }

  void set_read_only() {
    // Workspace writability is cached policy; OS permissions differ.
    auto *owner = const_cast<megatoy::workspace::Folder *>(
        preferences.workspace().find(folder));
    CHECK(owner != nullptr);
    owner->writable = false;
  }
};

} // namespace session_tests
