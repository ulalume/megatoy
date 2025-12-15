#include "patch_repository.hpp"
#include "formats/ctrmml.hpp"
#include "formats/patch_loader.hpp"
#include "patch_storage.hpp"
#include "patches/filesystem_patch_storage.hpp"
#include "platform/platform_config.hpp"
#include "ym2612/patch.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include "patches/web_patch_storage.hpp"
#endif
#include <algorithm>
#include <functional>
#include <iostream>
#include <string>

namespace patches {

PatchRepository::PatchRepository(platform::VirtualFileSystem &vfs,
                                 const std::filesystem::path &patches_root,
                                 const std::filesystem::path &builtin_dir,
                                 const std::filesystem::path &metadata_db_path)
    : patches_directory_(patches_root), builtin_patch_directory_(builtin_dir),
      vfs_(vfs), has_builtin_directory_(!builtin_dir.empty()),
      cache_initialized_(false) {

  // Initialize metadata manager if path is provided
  if (!metadata_db_path.empty()) {
    metadata_manager_ =
        std::make_unique<PatchMetadataManager>(metadata_db_path.string());
    if (!metadata_manager_->initialize()) {
      std::cerr << "Warning: Failed to initialize patch metadata database"
                << std::endl;
      metadata_manager_.reset();
    }
  }

#if defined(MEGATOY_PLATFORM_WEB)
  storages_.push_back(std::make_unique<WebPatchStorage>());
#endif
  storages_.push_back(std::make_unique<FilesystemPatchStorage>(
      vfs_, patches_directory_, "", metadata_manager_.get()));
  if (has_builtin_directory_) {
    storages_.push_back(std::make_unique<FilesystemPatchStorage>(
        vfs_, builtin_patch_directory_, kBuiltinRootName,
        metadata_manager_.get()));
  }

  refresh();
}

void PatchRepository::refresh() {
  tree_cache_.clear();

  user_time_valid_ =
      vfs_.is_directory(patches_directory_) &&
      vfs_.last_write_time(patches_directory_, last_user_directory_check_time_);
  if (has_builtin_directory_) {
    builtin_time_valid_ =
        vfs_.is_directory(builtin_patch_directory_) &&
        vfs_.last_write_time(builtin_patch_directory_,
                             last_builtin_directory_check_time_);
  } else {
    builtin_time_valid_ = false;
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
    size_t instrument_index = entry.ctrmml_index;
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

  bool changed = false;

  std::filesystem::file_time_type current_time{};
  if (vfs_.is_directory(patches_directory_)) {
    if (!vfs_.last_write_time(patches_directory_, current_time)) {
      changed = true;
    } else if (!user_time_valid_ ||
               current_time != last_user_directory_check_time_) {
      changed = true;
    }
  } else if (user_time_valid_) {
    changed = true;
  }

  if (has_builtin_directory_) {
    if (vfs_.is_directory(builtin_patch_directory_)) {
      if (!vfs_.last_write_time(builtin_patch_directory_, current_time)) {
        changed = true;
      } else if (!builtin_time_valid_ ||
                 current_time != last_builtin_directory_check_time_) {
        changed = true;
      }
    } else if (builtin_time_valid_) {
      changed = true;
    }
  }

  return changed;
}

std::vector<std::string> PatchRepository::supported_extensions() {
  return {".gin", ".ginpkg", ".rym2612", ".dmp", ".fui", ".mml"};
}

bool PatchRepository::remove_patch(const PatchEntry &entry) {
  for (const auto &storage : storages_) {
    if (storage->remove_patch(entry)) {
      refresh();
      return true;
    }
  }
  return false;
}

std::filesystem::path
PatchRepository::to_relative_path(const std::filesystem::path &path) const {
#if defined(MEGATOY_PLATFORM_WEB)
  constexpr std::string_view kLocalStorageRelativeRoot = "localStorage";
  const std::string generic = path.generic_string();
  if (!generic.empty() && generic.rfind(kLocalStorageRelativeRoot, 0) == 0) {
    return path;
  }
#endif
  if (has_builtin_directory_) {
    auto relative_builtin = path.lexically_relative(builtin_patch_directory_);
    if (!relative_builtin.empty() && relative_builtin.native()[0] != '.') {
      return std::filesystem::path(kBuiltinRootName) / relative_builtin;
    }
  }

  auto relative_user = path.lexically_relative(patches_directory_);
  if (!relative_user.empty() && relative_user.native()[0] != '.') {
    return relative_user;
  }

  return path;
}

std::filesystem::path
PatchRepository::to_absolute_path(const std::filesystem::path &path) const {
  std::string relative = path.generic_string();

#if defined(MEGATOY_PLATFORM_WEB)
  constexpr std::string_view kLocalStorageRelativeRoot = "localStorage";
  if (!relative.empty() && relative.rfind(kLocalStorageRelativeRoot, 0) == 0) {
    return path;
  }
#endif
  if (has_builtin_directory_) {
    const std::string builtin_root = kBuiltinRootName;
    if (relative == builtin_root) {
      return builtin_patch_directory_;
    }
    const std::string builtin_prefix = builtin_root + "/";
    if (relative.rfind(builtin_prefix, 0) == 0) {
      std::string without_prefix = relative.substr(builtin_prefix.size());
      return builtin_patch_directory_ / without_prefix;
    }
  }

  return patches_directory_ / path;
}

bool PatchRepository::save_patch_metadata(const std::string &relative_path,
                                          const ym2612::Patch &patch,
                                          const PatchMetadata &metadata) {
  if (!metadata_manager_) {
    return false;
  }

  // Calculate hash for the patch
  std::string hash = patch.hash();

  PatchMetadata metadata_with_hash = metadata;
  metadata_with_hash.path = relative_path;
  metadata_with_hash.hash = hash;

  return metadata_manager_->save_metadata(metadata_with_hash);
}

bool PatchRepository::update_patch_metadata(const std::string &relative_path,
                                            const PatchMetadata &metadata) {
  if (!metadata_manager_) {
    return false;
  }

  PatchMetadata updated_metadata = metadata;
  updated_metadata.path = relative_path;

  return metadata_manager_->update_metadata(updated_metadata);
}

std::optional<PatchMetadata>
PatchRepository::get_patch_metadata(const std::string &relative_path) const {
  if (!metadata_manager_) {
    return std::nullopt;
  }

  return metadata_manager_->get_metadata(relative_path);
}

std::vector<PatchEntry> PatchRepository::get_patches_by_metadata_filter(
    const std::function<bool(const PatchMetadata &)> &filter) const {
  std::vector<PatchEntry> result;

  if (!metadata_manager_) {
    return result;
  }

  // Get all metadata and filter
  auto all_metadata = metadata_manager_->get_all_metadata();

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
  if (!metadata_manager_) {
    return;
  }

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
  metadata_manager_->cleanup_missing_files(existing_paths);
}

} // namespace patches
