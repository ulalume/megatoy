#include "patches/background_folder_scan.hpp"

#if !defined(MEGATOY_PLATFORM_WEB)

#include "patches/filesystem_patch_storage.hpp"
#include "patches/patch_repository.hpp"
#include "platform/std_file_system.hpp"

#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace patches::background_folder_scan {

namespace {

struct ActiveScan {
  std::filesystem::path folder;
  std::string folder_name;
  ScanProgress progress;
  std::function<void(bool cancelled)> on_complete;
  std::thread worker;
};

PersistentParseCache *g_cache = nullptr;
std::unique_ptr<ActiveScan> g_active_scan;

std::string folder_display_name(const std::filesystem::path &folder) {
  auto name = folder.filename().string();
  if (name.empty()) {
    // A trailing separator leaves filename() empty.
    name = folder.parent_path().filename().string();
  }
  if (name.empty()) {
    name = folder.string();
  }
  return name;
}

} // namespace

void scan_and_warm(const std::filesystem::path &folder,
                   std::string_view root_label, PersistentParseCache *cache,
                   ScanProgress &progress) {
  platform::StdFileSystem file_system;
  FilesystemPatchStorage storage(file_system, folder, std::string(root_label),
                                 /*writable=*/true, /*enable_metadata=*/false,
                                 cache);
  FilesystemPatchStorage::ScanObserver observer;
  observer.on_file = [&progress, &storage](const std::filesystem::path &) {
    progress.files_seen.fetch_add(1, std::memory_order_relaxed);
    progress.containers_parsed.store(
        storage.container_parse_count_for_testing(), std::memory_order_relaxed);
    return !progress.cancel.load(std::memory_order_relaxed);
  };
  storage.set_scan_observer(std::move(observer));

  // The tree is thrown away: walking it is what fills the cache, and the
  // workspace builds its own tree from scratch afterwards.
  std::vector<PatchEntry> discarded;
  storage.append_entries(discarded);

  progress.containers_parsed.store(storage.container_parse_count_for_testing(),
                                   std::memory_order_relaxed);
  progress.finished.store(true, std::memory_order_release);
}

void configure(PersistentParseCache *cache) { g_cache = cache; }

bool begin(const std::filesystem::path &folder,
           std::function<void(bool cancelled)> on_complete) {
  if (g_active_scan) {
    return false;
  }

  auto scan = std::make_unique<ActiveScan>();
  scan->folder = folder;
  scan->folder_name = folder_display_name(folder);
  scan->on_complete = std::move(on_complete);

  auto *raw = scan.get();
  g_active_scan = std::move(scan);
  g_active_scan->worker = std::thread([raw, cache = g_cache] {
    scan_and_warm(raw->folder, raw->folder_name, cache, raw->progress);
  });
  return true;
}

Status status() {
  Status result;
  if (!g_active_scan) {
    return result;
  }
  result.active = true;
  result.files_seen =
      g_active_scan->progress.files_seen.load(std::memory_order_relaxed);
  result.containers_parsed =
      g_active_scan->progress.containers_parsed.load(std::memory_order_relaxed);
  result.folder_name = g_active_scan->folder_name;
  return result;
}

void request_cancel() {
  if (g_active_scan) {
    g_active_scan->progress.cancel.store(true, std::memory_order_relaxed);
  }
}

void poll_completion() {
  if (!g_active_scan ||
      !g_active_scan->progress.finished.load(std::memory_order_acquire)) {
    return;
  }

  // Clear the slot before the callback runs: it is on the UI thread and may
  // well start the next scan.
  auto finished = std::move(g_active_scan);
  g_active_scan.reset();
  if (finished->worker.joinable()) {
    finished->worker.join();
  }
  if (finished->on_complete) {
    finished->on_complete(
        finished->progress.cancel.load(std::memory_order_relaxed));
  }
}

void shutdown() {
  if (!g_active_scan) {
    return;
  }
  g_active_scan->progress.cancel.store(true, std::memory_order_relaxed);
  if (g_active_scan->worker.joinable()) {
    g_active_scan->worker.join();
  }
  g_active_scan.reset();
}

} // namespace patches::background_folder_scan

#endif
