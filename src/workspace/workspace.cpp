#include "workspace/workspace.hpp"

#include "workspace/path_policy.hpp"

#include <algorithm>
#include <system_error>

namespace megatoy::workspace {

namespace fs = std::filesystem;

namespace {
constexpr const char *kSidecarDirectory = ".megatoy";
constexpr const char *kSidecarFile = "patches.json";
} // namespace

fs::path Folder::metadata_path() const {
  return path / kSidecarDirectory / kSidecarFile;
}

bool Workspace::probe_writable(const fs::path &path) {
  std::error_code ec;
  const auto status = fs::status(path, ec);
  if (ec || !fs::is_directory(status)) {
    return false;
  }
  return (status.permissions() & fs::perms::owner_write) != fs::perms::none;
}

namespace {

Folder make_folder(const fs::path &resolved) {
  Folder folder;
  folder.path = resolved;
  folder.name = resolved.filename().string();
  if (folder.name.empty()) {
    // A root path such as "/" has no filename component.
    folder.name = resolved.string();
  }
  return folder;
}

} // namespace

bool Workspace::add(const fs::path &path) {
  const auto resolved = normalize_path(path);

  std::error_code ec;
  if (!fs::is_directory(resolved, ec) || ec) {
    return false;
  }
  if (contains(resolved)) {
    return false;
  }

  Folder folder = make_folder(resolved);
  folder.available = true;
  folder.writable = probe_writable(resolved);

  folders_.push_back(std::move(folder));
  ++revision_;
  return true;
}

bool Workspace::remove(const fs::path &path) {
  const auto resolved = normalize_path(path);
  const auto it =
      std::find_if(folders_.begin(), folders_.end(), [&](const Folder &folder) {
        return folder.path == resolved;
      });
  if (it == folders_.end()) {
    return false;
  }
  folders_.erase(it);
  ++revision_;
  return true;
}

bool Workspace::reorder(std::size_t from, std::size_t to) {
  if (from >= folders_.size() || to >= folders_.size() || from == to) {
    return false;
  }
  Folder moved = std::move(folders_[from]);
  folders_.erase(folders_.begin() + static_cast<std::ptrdiff_t>(from));
  folders_.insert(folders_.begin() + static_cast<std::ptrdiff_t>(to),
                  std::move(moved));
  ++revision_;
  return true;
}

bool Workspace::contains(const fs::path &path) const {
  const auto resolved = normalize_path(path);
  return std::any_of(
      folders_.begin(), folders_.end(),
      [&](const Folder &folder) { return folder.path == resolved; });
}

const Folder *Workspace::find(const fs::path &path) const {
  const auto resolved = normalize_path(path);
  const auto it =
      std::find_if(folders_.begin(), folders_.end(), [&](const Folder &folder) {
        return folder.path == resolved;
      });
  return it == folders_.end() ? nullptr : &*it;
}

bool Workspace::rename(const fs::path &from, const fs::path &to) {
  const auto it =
      std::find_if(folders_.begin(), folders_.end(),
                   [&](const Folder &folder) { return folder.path == from; });
  if (it == folders_.end()) {
    return false;
  }

  const auto resolved_to = normalize_path(to);
  const bool taken =
      std::any_of(folders_.begin(), folders_.end(), [&](const Folder &folder) {
        return &folder != &*it && folder.path == resolved_to;
      });
  if (taken) {
    return false;
  }

  const bool was_writable = it->writable;
  *it = make_folder(resolved_to);
  it->available = true;
  it->writable = was_writable;
  ++revision_;
  return true;
}

const Folder *Workspace::owner_of(const fs::path &path) const {
  const auto resolved = normalize_path(path);
  const Folder *best = nullptr;
  std::size_t best_length = 0;

  for (const auto &folder : folders_) {
    const auto relative = resolved.lexically_relative(folder.path);
    if (relative.empty() || *relative.begin() == "..") {
      continue;
    }
    // Nested folders are allowed, so the deepest match wins.
    const std::size_t length = folder.path.native().size();
    if (best == nullptr || length > best_length) {
      best = &folder;
      best_length = length;
    }
  }
  return best;
}

std::optional<fs::path> Workspace::default_save_folder() const {
  for (const auto &folder : folders_) {
    if (folder.available && folder.writable) {
      return folder.path;
    }
  }
  return std::nullopt;
}

void Workspace::set_paths(const std::vector<fs::path> &paths) {
  folders_.clear();
  for (const auto &path : paths) {
    const auto resolved = normalize_path(path);
    if (contains(resolved)) {
      continue;
    }
    Folder folder = make_folder(resolved);
    std::error_code ec;
    folder.available = fs::is_directory(resolved, ec) && !ec;
    folder.writable = folder.available && probe_writable(resolved);
    folders_.push_back(std::move(folder));
  }
  ++revision_;
}

std::vector<fs::path> Workspace::paths() const {
  std::vector<fs::path> result;
  result.reserve(folders_.size());
  for (const auto &folder : folders_) {
    result.push_back(folder.path);
  }
  return result;
}

bool Workspace::refresh() {
  bool changed = false;
  for (auto &folder : folders_) {
    std::error_code ec;
    const bool available = fs::is_directory(folder.path, ec) && !ec;
    const bool writable = available && probe_writable(folder.path);
    if (available != folder.available || writable != folder.writable) {
      folder.available = available;
      folder.writable = writable;
      changed = true;
    }
  }
  if (changed) {
    ++revision_;
  }
  return changed;
}

} // namespace megatoy::workspace
