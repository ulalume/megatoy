#include "patch_repository.hpp"
#include "formats/ctrmml.hpp"
#include "formats/patch_loader.hpp"
#include "patch_storage.hpp"
#include "platform/platform_config.hpp"
#include "patches/filesystem_patch_storage.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include "patches/web_patch_storage.hpp"
#endif
#include "ym2612/patch.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>

namespace patches {

PatchRepository::PatchRepository(platform::VirtualFileSystem &vfs,
                                 const std::filesystem::path &patches_root,
                                 const std::filesystem::path &user_save_root,
                                 const std::filesystem::path &builtin_dir,
                                 const std::filesystem::path &metadata_db_path)
    : patches_root_directory_(patches_root),
      user_save_directory_(user_save_root), builtin_patch_directory_(builtin_dir),
      vfs_(vfs),
      has_builtin_directory_(!builtin_dir.empty()), cache_initialized_(false) {

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
      vfs_, patches_root_directory_, "", metadata_manager_.get(), true,
      user_save_directory_));
  if (has_builtin_directory_) {
    storages_.push_back(std::make_unique<FilesystemPatchStorage>(
        vfs_, builtin_patch_directory_, kBuiltinRootName,
        metadata_manager_.get(), false));
  }

  refresh();
}

void PatchRepository::refresh() {
  tree_cache_.clear();

  user_time_valid_ =
      vfs_.is_directory(patches_root_directory_) &&
      vfs_.last_write_time(patches_root_directory_,
                           last_user_directory_check_time_);
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
  if (vfs_.is_directory(patches_root_directory_)) {
    if (!vfs_.last_write_time(patches_root_directory_, current_time)) {
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

SavePatchResult PatchRepository::save_patch(const ym2612::Patch &patch,
                                            const std::string &name,
                                            bool overwrite,
                                            std::string_view preferred_extension) {
  for (const auto &storage : storages_) {
    auto result =
        storage->save_patch(patch, name, overwrite, preferred_extension);
    if (result.status != SavePatchResult::Status::Unsupported) {
      if (result.status == SavePatchResult::Status::Success) {
        refresh();
      }
      return result;
    }
  }
  return SavePatchResult::unsupported();
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
  return patches_root_directory_ / path;
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

bool PatchRepository::download_patch(const ym2612::Patch &patch,
                                     const std::string &name,
                                     const std::string &extension_hint) const {
  for (const auto &storage : storages_) {
    if (storage->download_patch(patch, name, extension_hint)) {
      return true;
    }
  }
  return false;
}

} // namespace patches
