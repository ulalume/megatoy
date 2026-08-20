#pragma once

#include "platform/platform_config.hpp"

#if !defined(MEGATOY_PLATFORM_WEB)

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace patches {

class PersistentParseCache;

/**
 * Parse a folder the user just picked on a worker thread, before it joins the
 * workspace.
 *
 * The worker shares no storage and no tree with the UI: all it does is warm
 * the persistent parse cache. Once it is done the folder is added normally
 * and the ordinary synchronous sync walks it again -- every container is a
 * cache hit by then, so that walk is cheap. Without this, adding a folder of
 * a few thousand banks froze the app for the whole first parse.
 *
 * Desktop only: the browser has no folder to point at (it imports a copy
 * instead) and no threads to spare.
 *
 * `configure`, `begin`, `status`, `request_cancel`, `poll_completion` and
 * `shutdown` are UI-thread-only; the worker touches nothing but ScanProgress
 * and the cache's own locked interface.
 */
namespace background_folder_scan {

struct ScanProgress {
  std::atomic<std::size_t> files_seen{0};
  std::atomic<std::size_t> containers_parsed{0};
  std::atomic<bool> cancel{false};
  std::atomic<bool> finished{false};
};

struct Status {
  bool active = false;
  std::size_t files_seen = 0;
  std::size_t containers_parsed = 0;
  std::string folder_name;
};

/**
 * Walk `folder` and store every container it holds in `cache`.
 *
 * Synchronous and self-contained -- the thread wrapper below is the only
 * reason it is not called directly.
 *
 * `root_label` has to match the label the workspace will give the folder, or
 * the warmed entries do not match on lookup. That is the folder's basename in
 * the common case; when a second folder shares that basename the workspace
 * uniquifies the label and this folder simply parses again, which costs time
 * and nothing else.
 */
void scan_and_warm(const std::filesystem::path &folder,
                   std::string_view root_label, PersistentParseCache *cache,
                   ScanProgress &progress);

/// Cache every later scan warms. Called once at app init.
void configure(PersistentParseCache *cache);

/**
 * Start warming `folder` on a worker thread.
 *
 * Returns false, having done nothing, when a scan is already running.
 * `on_complete` runs on the UI thread from `poll_completion`, exactly once.
 */
bool begin(const std::filesystem::path &folder,
           std::function<void(bool cancelled)> on_complete);

Status status();
void request_cancel();

/// Join a finished worker and run its callback. Called every frame.
void poll_completion();

/// Cancel and join a running scan without running its callback.
void shutdown();

} // namespace background_folder_scan

} // namespace patches

#endif
