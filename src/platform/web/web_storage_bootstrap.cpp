#include "platform/web/web_storage_bootstrap.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include "core/status.hpp"
#include "platform/web/web_folder_delete.hpp"
#include "platform/web/web_storage_persistence.hpp"
#include "system/path_service.hpp"
#include "workspace/path_policy.hpp"
#include "workspace/workspace.hpp"

#include <filesystem>
#include <system_error>
#include <vector>

namespace platform::web {

std::filesystem::path default_workspace_folder() {
  return megatoy::system::PathService::web_storage_root() / kDefaultFolderName;
}

bool bootstrap_workspace(megatoy::workspace::Workspace &workspace,
                         const std::filesystem::path &storage_root) {
  if (storage_load_failed()) {
    megatoy::status::error(
        "Could not read browser storage -- running without saving. Your "
        "stored patches are untouched; reload to retry.");
    if (storage_root.empty()) {
      return false;
    }

    std::error_code ec;
    const auto home = storage_root / kDefaultFolderName;
    std::filesystem::create_directories(home, ec);
    if (ec) {
      return false;
    }
    return workspace.contains(home) ? false : workspace.add(home);
  }

  if (storage_root.empty()) {
    return false;
  }

  std::error_code ec;
  std::filesystem::create_directories(storage_root, ec);
  if (ec) {
    megatoy::status::error(
        "Persistent storage unavailable -- changes will not survive a "
        "reload. (" +
        ec.message() + ")");
    return false;
  }

  const auto home = storage_root / kDefaultFolderName;
  const bool existed = std::filesystem::is_directory(home, ec);
  if (!existed) {
    std::filesystem::create_directories(home, ec);
    if (ec) {
      megatoy::status::error("Could not create \"My Patches\": " +
                             ec.message());
      return false;
    }
  }

  // A tab closed mid-import can leave staging behind, and a concurrent
  // debounced sync may already have persisted it. Sweep it before the
  // re-adoption loop below could mistake it for a library folder.
  std::size_t swept = 0;
  {
    std::vector<std::filesystem::path> stale_staging;
    std::filesystem::directory_iterator stale(storage_root, ec);
    const std::filesystem::directory_iterator stale_end;
    while (!ec && stale != stale_end) {
      if (stale->path().filename().string().starts_with(".import-tmp-")) {
        stale_staging.push_back(stale->path());
      }
      stale.increment(ec);
    }
    ec.clear();
    for (const auto &path : stale_staging) {
      std::error_code remove_error;
      std::filesystem::remove_all(path, remove_error);
      if (!remove_error) {
        ++swept;
      }
    }
  }

  // A folder deletion whose flush never committed -- the tab reloaded during
  // the tens of seconds a large folder takes to leave IndexedDB -- left a
  // tombstone behind, and the populate above has just handed every one of
  // those files back. Replay the deletion here, before the re-adoption loop
  // below could mistake the folder for a library the user still has.
  //
  // The tombstone deliberately stays: only a committed flush proves the files
  // are gone for good, and that is where on_persist_succeeded() retires it.
  bool pending_deletions_remain = false;
  for (const auto &pending : pending_deletions()) {
    // Same guard as the delete path itself: only a directory sitting directly
    // inside the storage root is ever something this app deleted. Anything
    // else is a stale or mangled entry, and dropping it keeps the list from
    // growing without bound.
    if (!megatoy::workspace::paths_equal(pending.parent_path(), storage_root)) {
      clear_pending_deletion(pending);
      continue;
    }
    pending_deletions_remain = true;
    std::error_code remove_error;
    std::filesystem::remove_all(pending, remove_error);
  }

  bool changed = false;
  if (!workspace.contains(home)) {
    changed = workspace.add(home);
  }

  // Folders imported in an earlier session are still on disk but only
  // referenced from preferences. If preferences were cleared, re-adopt them so
  // the library is not silently invisible.
  std::filesystem::directory_iterator entry(storage_root, ec);
  const std::filesystem::directory_iterator end;
  while (!ec && entry != end) {
    std::error_code type_error;
    const bool is_directory = entry->is_directory(type_error);
    if (type_error || !is_directory ||
        entry->path().filename().string().starts_with(".") ||
        workspace.contains(entry->path())) {
      entry.increment(ec);
      continue;
    }
    if (workspace.add(entry->path())) {
      changed = true;
    }
    entry.increment(ec);
  }

  // A surviving tombstone always needs a flush, whether or not this startup
  // found anything left to remove: the flush is what retires it.
  if (changed || swept > 0 || pending_deletions_remain || !existed) {
    request_storage_persist();
  }
  return changed;
}

} // namespace platform::web

#endif
