#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <vector>

namespace patches {

/**
 * Notices when anything under a set of folders changes outside the app.
 * Notices arrive on a background thread and are only noted there; they are
 * picked up with take_settled_change(). Hidden entries, such as the
 * .DS_Store Finder writes when a folder is merely looked at, are ignored.
 * In the browser there is nothing outside to watch, and it never reports.
 */
class FolderWatcher {
public:
  FolderWatcher();
  ~FolderWatcher();
  FolderWatcher(const FolderWatcher &) = delete;
  FolderWatcher &operator=(const FolderWatcher &) = delete;

  /// Watch these folders and everything below them, in place of any before.
  void watch(const std::vector<std::filesystem::path> &folders);

  /**
   * True once something has changed and nothing more has for `quiet`, so a
   * copy of many files is taken as one change. Clears it.
   */
  bool take_settled_change(
      std::chrono::milliseconds quiet = std::chrono::milliseconds(300));

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace patches
