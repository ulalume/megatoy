#include "patch_repository.hpp"
#include "formats/patch_loader.hpp"
#include "formats/ym2612_format_adapter.hpp"
#include "patch_storage.hpp"
#include "patches/filesystem_patch_storage.hpp"
#include "platform/platform_config.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include "platform/web/web_storage_persistence.hpp"
#endif
#include "ym2612/patch.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>

namespace patches {

PatchRepository::PatchRepository(
    platform::VirtualFileSystem &vfs,
    const megatoy::workspace::Workspace &workspace,
    const std::filesystem::path &builtin_presets_dir)
    : workspace_(workspace), builtin_presets_directory_(builtin_presets_dir),
      vfs_(vfs) {
  rebuild_storages();
  refresh();
}

void PatchRepository::set_show_builtin_presets(bool show) {
  if (show == show_builtin_presets_) {
    return;
  }
  show_builtin_presets_ = show;
  rebuild_storages();
  refresh();
}

bool PatchRepository::sync_workspace() {
  if (storages_built_ && synced_revision_ == workspace_.revision()) {
    return false;
  }
  rebuild_storages();
  refresh();
  return true;
}

void PatchRepository::rebuild_storages() {
  storages_.clear();
  watched_directories_.clear();

  // Root labels prefix every relative path in the tree, so they have to be
  // unique even when two folders share a basename.
  std::vector<std::string> used_labels;
  auto unique_label = [&used_labels](std::string base) {
    if (base.empty()) {
      base = "folder";
    }
    std::string candidate = base;
    for (int suffix = 2; std::find(used_labels.begin(), used_labels.end(),
                                   candidate) != used_labels.end();
         ++suffix) {
      candidate = base + " (" + std::to_string(suffix) + ")";
    }
    used_labels.push_back(candidate);
    return candidate;
  };

  // The presets label is reserved so a user folder called "presets" cannot
  // shadow the built-in one.
  used_labels.emplace_back(kBuiltinRootName);

  for (const auto &folder : workspace_.folders()) {
    storages_.push_back(std::make_unique<FilesystemPatchStorage>(
        vfs_, folder.path, unique_label(folder.name), folder.writable,
        /*enable_metadata=*/folder.writable));
    if (folder.available) {
      watched_directories_.push_back(folder.path);
    }
  }

  if (show_builtin_presets_ && !builtin_presets_directory_.empty()) {
    storages_.push_back(std::make_unique<FilesystemPatchStorage>(
        vfs_, builtin_presets_directory_, kBuiltinRootName,
        /*writable=*/false, /*enable_metadata=*/false));
    watched_directories_.push_back(builtin_presets_directory_);
  }

  synced_revision_ = workspace_.revision();
  storages_built_ = true;
}

void PatchRepository::refresh() {
  tree_cache_.clear();

  watched_times_.assign(watched_directories_.size(),
                        std::filesystem::file_time_type{});
  for (std::size_t i = 0; i < watched_directories_.size(); ++i) {
    vfs_.last_write_time(watched_directories_[i], watched_times_[i]);
  }

  for (const auto &storage : storages_) {
    storage->append_entries(tree_cache_);
  }

  cache_initialized_ = true;
}

const std::vector<PatchEntry> &PatchRepository::tree() const {
  return tree_cache_;
}

bool PatchRepository::load_patch(const PatchEntry &entry,
                                 ym2612::Patch &patch) const {
  if (entry.is_directory) {
    return false;
  }

  for (const auto &storage : storages_) {
    if (storage->load_patch(entry, patch)) {
      return true;
    }
  }

  auto result = formats::load_patch_from_file(entry.full_path);
  if (result.status == formats::PatchLoadStatus::Failure) {
    std::cerr << "Error loading preset patch " << entry.full_path << std::endl;
    return false;
  } else if (result.status == formats::PatchLoadStatus::Success) {
    patch = result.patches[0];
    return true;
  }
  try {
    size_t instrument_index = entry.instrument_index;
    if (instrument_index >= result.patches.size()) {
      return false;
    }
    patch = result.patches[instrument_index];
    return true;
  } catch (const std::exception &e) {
    std::cerr << "Error loading preset patch " << entry.full_path << ": "
              << e.what() << std::endl;
    return false;
  }
  return false;
}

bool PatchRepository::has_directory_changed() const {
  if (!cache_initialized_) {
    return true;
  }
  if (watched_times_.size() != watched_directories_.size()) {
    return true;
  }

  for (std::size_t i = 0; i < watched_directories_.size(); ++i) {
    std::filesystem::file_time_type current{};
    if (!vfs_.last_write_time(watched_directories_[i], current)) {
      // The folder went away, or came back after being unreadable.
      if (watched_times_[i] != std::filesystem::file_time_type{}) {
        return true;
      }
      continue;
    }
    if (current != watched_times_[i]) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> PatchRepository::supported_extensions() {
  auto extensions = formats::adapter::readable_extensions();
  extensions.push_back(".ginpkg");
  return extensions;
}

SavePatchResult
PatchRepository::save_patch(const ym2612::Patch &patch, const std::string &name,
                            bool overwrite,
                            std::string_view preferred_extension) {
  for (const auto &storage : storages_) {
    auto result =
        storage->save_patch(patch, name, overwrite, preferred_extension);
    if (result.status != SavePatchResult::Status::Unsupported) {
      if (result.status == SavePatchResult::Status::Success) {
        refresh();
#if defined(MEGATOY_PLATFORM_WEB)
        platform::web::request_storage_persist();
#endif
      }
      return result;
    }
  }
  return SavePatchResult::unsupported();
}

SavePatchResult
PatchRepository::save_patch_in(const std::filesystem::path &folder,
                               const ym2612::Patch &patch,
                               const std::string &name, bool overwrite,
                               std::string_view preferred_extension) {
  for (const auto &storage : storages_) {
    auto *filesystem_storage =
        dynamic_cast<FilesystemPatchStorage *>(storage.get());
    if (filesystem_storage == nullptr || filesystem_storage->root() != folder) {
      continue;
    }
    auto result = filesystem_storage->save_patch(patch, name, overwrite,
                                                 preferred_extension);
    if (result.status == SavePatchResult::Status::Success) {
      refresh();
    }
    return result;
  }
  return SavePatchResult::unsupported();
}

std::filesystem::path
PatchRepository::to_relative_path(const std::filesystem::path &path) const {
  for (const auto &storage : storages_) {
    if (auto mapped = storage->to_relative_path(path)) {
      return *mapped;
    }
  }
  return path;
}

std::filesystem::path
PatchRepository::to_absolute_path(const std::filesystem::path &path) const {
  for (const auto &storage : storages_) {
    if (auto mapped = storage->to_absolute_path(path)) {
      return *mapped;
    }
  }
  // Nothing claimed it, so there is no folder to resolve against.
  return path;
}

bool PatchRepository::save_patch_metadata(const std::string &relative_path,
                                          const ym2612::Patch &patch,
                                          const PatchMetadata &metadata) {
  for (const auto &storage : storages_) {
    if (storage->save_patch_metadata(relative_path, patch, metadata)) {
      return true;
    }
  }
  return false;
}

bool PatchRepository::update_patch_metadata(const std::string &relative_path,
                                            const PatchMetadata &metadata) {
  for (const auto &storage : storages_) {
    if (storage->update_patch_metadata(relative_path, metadata)) {
      return true;
    }
  }
  return false;
}

std::optional<PatchMetadata>
PatchRepository::get_patch_metadata(const std::string &relative_path) const {
  for (const auto &storage : storages_) {
    if (auto md = storage->get_patch_metadata(relative_path)) {
      return md;
    }
  }
  return std::nullopt;
}

std::vector<PatchEntry> PatchRepository::get_patches_by_metadata_filter(
    const std::function<bool(const PatchMetadata &)> &filter) const {
  std::vector<PatchEntry> result;

  std::function<void(const std::vector<PatchEntry> &)> search_tree;
  search_tree = [&](const std::vector<PatchEntry> &entries) {
    for (const auto &entry : entries) {
      if (entry.is_directory) {
        search_tree(entry.children);
      } else if (entry.metadata && filter(*entry.metadata)) {
        result.push_back(entry);
      }
    }
  };

  search_tree(tree_cache_);
  return result;
}

void PatchRepository::cleanup_orphaned_metadata() {
  // Collect all existing patch paths
  std::vector<std::string> existing_paths;

  std::function<void(const std::vector<PatchEntry> &)> collect_paths;
  collect_paths = [&](const std::vector<PatchEntry> &entries) {
    for (const auto &entry : entries) {
      if (entry.is_directory) {
        collect_paths(entry.children);
      } else {
        existing_paths.push_back(entry.relative_path);
      }
    }
  };

  collect_paths(tree_cache_);

  // Cleanup orphaned entries
  for (const auto &storage : storages_) {
    storage->cleanup_metadata(existing_paths);
  }
}

std::string PatchRepository::primary_writable_label() const {
  for (const auto &storage : storages_) {
    if (storage->is_writable()) {
      std::string label(storage->label());
      if (!label.empty()) {
        return label;
      }
    }
  }
  return "user";
}

bool PatchRepository::patch_name_conflicts(const std::string &name) const {
  for (const auto &storage : storages_) {
    if (auto has = storage->has_patch_named(name)) {
      if (*has) {
        return true;
      }
    }
  }
  return false;
}

} // namespace patches
