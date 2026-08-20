#pragma once

#include "platform/platform_config.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace platform::web {

/**
 * Delete a workspace folder from browser storage and wait for IndexedDB to
 * agree that it is gone.
 *
 * Removing the files from MEMFS is instant, but MEMFS is just wasm memory:
 * until a `FS.syncfs(false, ...)` transaction commits, IndexedDB still holds
 * every one of them. That flush is not fast -- measured at roughly 4.5s for
 * 10k files, and superlinear beyond that -- so the debounced fire-and-forget
 * persist used elsewhere left a window of tens of seconds in which a reload
 * aborted the transaction, IndexedDB kept the whole folder, and startup
 * re-adopted it. The folder came back.
 *
 * So the deletion is awaited instead: `on_complete` runs on a later frame,
 * on the main thread, once the flush has actually committed (or failed).
 * Callers should not remove the folder from the workspace until then.
 *
 * Returns false -- without touching anything -- when a deletion is already
 * running; there is one flush in flight at a time.
 */
bool begin_folder_delete(
    const std::filesystem::path &path,
    std::function<void(bool ok, std::string error)> on_complete);

/// Draw the modal shown while a deletion waits for browser storage.
void render_folder_delete_ui();

/**
 * Deletion tombstones.
 *
 * A tombstone is written *before* the files leave MEMFS and cleared only once
 * a flush has committed, so a reload landing anywhere in between finds a
 * record of what the user asked to delete. Startup replays it. Without this a
 * reload during the flush is indistinguishable from never having deleted
 * anything at all.
 *
 * Stored in localStorage rather than the filesystem being deleted, for the
 * obvious reason.
 */
void add_pending_deletion(const std::filesystem::path &path);
void clear_pending_deletion(const std::filesystem::path &path);
std::vector<std::filesystem::path> pending_deletions();

/**
 * Retire the tombstones a committed flush has made good on.
 *
 * Called from the debounced persist's success path, which is what a startup
 * replay ends up scheduling: the replay re-deletes and leaves the tombstone
 * in place, and only the flush that follows can prove the deletion reached
 * IndexedDB.
 */
void on_persist_succeeded();

} // namespace platform::web

#endif
