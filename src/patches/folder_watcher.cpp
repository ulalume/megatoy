#include "folder_watcher.hpp"
#include "workspace/path_policy.hpp"

#if !defined(__EMSCRIPTEN__)
#include <efsw/efsw.hpp>
#include <map>
#include <mutex>
#include <string>
#endif

namespace patches {

#if defined(__EMSCRIPTEN__)

struct FolderWatcher::Impl {};

FolderWatcher::FolderWatcher() = default;
FolderWatcher::~FolderWatcher() = default;
void FolderWatcher::watch(const std::vector<std::filesystem::path> &) {}
bool FolderWatcher::take_settled_change(std::chrono::milliseconds) {
  return false;
}

#else

namespace {

/// Whether a path below the watched folder passes through a hidden entry.
bool is_hidden(const std::filesystem::path &relative) {
  for (const auto &part : relative) {
    const auto name = part.string();
    if (!name.empty() && name.front() == '.' && name != "." && name != "..") {
      return true;
    }
  }
  return false;
}

} // namespace

struct FolderWatcher::Impl : efsw::FileWatchListener {
  using Clock = std::chrono::steady_clock;

  efsw::FileWatcher watcher;
  bool started = false;
  std::map<efsw::WatchID, std::filesystem::path> roots;

  std::mutex mutex;
  bool pending = false;
  Clock::time_point last_change{};

  void handleFileAction(efsw::WatchID id, const std::string &dir,
                        const std::string &filename, efsw::Action,
                        const std::string &) override {
    std::lock_guard lock(mutex);
    const auto root = roots.find(id);
    if (root != roots.end()) {
      const auto relative = (std::filesystem::path(dir) / filename)
                                .lexically_relative(root->second);
      if (is_hidden(relative)) {
        return;
      }
    }
    pending = true;
    last_change = Clock::now();
  }
};

FolderWatcher::FolderWatcher() : impl_(std::make_unique<Impl>()) {}

FolderWatcher::~FolderWatcher() {
  // The listener must outlive the watcher's thread, which stops with it.
  for (const auto &[id, root] : impl_->roots) {
    impl_->watcher.removeWatch(id);
  }
}

void FolderWatcher::watch(const std::vector<std::filesystem::path> &folders) {
  // Resolved, since a notice names the real path: /var on macOS is a link
  // to /private/var.
  std::vector<std::filesystem::path> wanted;
  for (const auto &folder : folders) {
    wanted.push_back(megatoy::workspace::normalize_path(folder));
  }
  {
    std::lock_guard lock(impl_->mutex);
    std::vector<std::filesystem::path> current;
    for (const auto &[id, root] : impl_->roots) {
      current.push_back(root);
    }
    if (current == wanted) {
      return;
    }
  }
  std::vector<efsw::WatchID> old_ids;
  {
    std::lock_guard lock(impl_->mutex);
    for (const auto &[id, root] : impl_->roots) {
      old_ids.push_back(id);
    }
    impl_->roots.clear();
  }
  for (const auto id : old_ids) {
    impl_->watcher.removeWatch(id);
  }
  for (const auto &folder : wanted) {
    const efsw::WatchID id = impl_->watcher.addWatch(
        folder.string(), impl_.get(), /*recursive=*/true);
    if (id > 0) {
      std::lock_guard lock(impl_->mutex);
      impl_->roots[id] = folder;
    }
  }
  if (!impl_->started) {
    impl_->watcher.watch();
    impl_->started = true;
  }
}

bool FolderWatcher::take_settled_change(std::chrono::milliseconds quiet) {
  std::lock_guard lock(impl_->mutex);
  if (!impl_->pending || Impl::Clock::now() - impl_->last_change < quiet) {
    return false;
  }
  impl_->pending = false;
  return true;
}

#endif

} // namespace patches
