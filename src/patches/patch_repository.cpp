#include "patch_repository.hpp"
#include "formats/patch_loader.hpp"
#include "formats/ym2612_format_adapter.hpp"
#include "patches/filename_utils.hpp"
#include "patches/filesystem_patch_storage.hpp"
#include "patches/patch_entry_actions.hpp"
#include "patches/patch_write.hpp"
#include "patches/persistent_parse_cache.hpp"
#include "platform/import_pipeline.hpp"
#include "platform/platform_config.hpp"
#include "platform/trash.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include "platform/web/web_storage_persistence.hpp"
#endif
#include "ym2612/patch.hpp"
#include <algorithm>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace patches {

PatchRepository::PatchRepository(
    platform::StdFileSystem &vfs,
    const megatoy::workspace::Workspace &workspace,
    const std::filesystem::path &builtin_presets_dir,
    const std::filesystem::path &builtin_presets_metadata,
    PersistentParseCache *persistent_cache)
    : workspace_(workspace), builtin_presets_directory_(builtin_presets_dir),
      builtin_presets_metadata_(builtin_presets_metadata), vfs_(vfs),
      persistent_cache_(persistent_cache) {
  rebuild_storages();
  refresh();
}

PatchRepository::~PatchRepository() = default;

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
    if (folder.writable) {
      sidecar::migrate_folder(folder.path);
    }
    auto storage = std::make_unique<FilesystemPatchStorage>(
        vfs_, folder.path, unique_label(folder.name), folder.writable,
        persistent_cache_);
    storage->set_listed_as(folder.name, workspace_.telling_parents(folder));
    storages_.push_back(std::move(storage));
    if (folder.available) {
      watched_directories_.push_back(folder.path);
    }
  }

  if (show_builtin_presets_ && !builtin_presets_directory_.empty()) {
    // The sidecars the presets ship with are the base under the user's own.
    storages_.push_back(std::make_unique<FilesystemPatchStorage>(
        vfs_, builtin_presets_directory_, kBuiltinRootName,
        /*writable=*/false, persistent_cache_, builtin_presets_metadata_));
    watched_directories_.push_back(builtin_presets_directory_);
  }

  // The built-in presets ship with the app; only the user's folders change.
  std::vector<std::filesystem::path> user_folders;
  for (const auto &folder : workspace_.folders()) {
    if (folder.available) {
      user_folders.push_back(folder.path);
    }
  }
  folder_watcher_.watch(user_folders);

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
  index_tree();

  if (persistent_cache_ && persistent_cache_->dirty() &&
      persistent_cache_->save()) {
#if defined(MEGATOY_PLATFORM_WEB)
    platform::web::request_storage_persist();
#endif
  }

  cache_initialized_ = true;
  ++revision_;
}

void PatchRepository::index_tree() {
  by_path_.clear();
  by_source_path_.clear();
  const auto add = [this](const auto &self,
                          const std::vector<PatchEntry> &entries) -> void {
    for (const auto &entry : entries) {
      if (!entry.is_directory) {
        by_path_.emplace(entry.relative_path, &entry);
        by_source_path_.emplace(entry.source_relative_path, &entry);
      }
      self(self, entry.children);
    }
  };
  add(add, tree_cache_);
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

bool PatchRepository::take_outside_change() {
  return folder_watcher_.take_settled_change();
}

std::vector<std::string> PatchRepository::supported_extensions() {
  const auto &extensions = platform::import_pipeline::supported_extensions();
  return {extensions.begin(), extensions.end()};
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
        changed();
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
    if (storage->root() != folder) {
      continue;
    }
    auto result =
        storage->save_patch(patch, name, overwrite, preferred_extension);
    if (result.status == SavePatchResult::Status::Success) {
      changed();
    }
    return result;
  }
  return SavePatchResult::unsupported();
}

bool PatchRepository::can_delete_patch(const PatchEntry &entry) const {
  return std::any_of(
      storages_.begin(), storages_.end(),
      [&](const auto &storage) { return storage->can_delete_patch(entry); });
}

bool PatchRepository::delete_patch(const PatchEntry &entry) {
  for (const auto &storage : storages_) {
    if (!storage->can_delete_patch(entry)) {
      continue;
    }
    if (!storage->delete_patch(entry)) {
      return false;
    }
    changed();
    return true;
  }
  return false;
}

bool PatchRepository::can_rename_patch(const PatchEntry &entry) const {
  return std::any_of(
      storages_.begin(), storages_.end(),
      [&](const auto &storage) { return storage->can_rename_patch(entry); });
}

bool PatchRepository::rename_patch(const PatchEntry &entry,
                                   const std::string &new_stem) {
  for (const auto &storage : storages_) {
    if (!storage->can_rename_patch(entry)) {
      continue;
    }
    if (!storage->rename_patch(entry, new_stem)) {
      return false;
    }
    changed();
    return true;
  }
  return false;
}

bool PatchRepository::can_move_patch(const PatchEntry &entry,
                                     const PatchEntry &destination) const {
  return std::any_of(storages_.begin(), storages_.end(),
                     [&](const auto &storage) {
                       return storage->can_move_patch(entry, destination);
                     });
}

bool PatchRepository::move_patch(const PatchEntry &entry,
                                 const PatchEntry &destination) {
  for (const auto &storage : storages_) {
    if (!storage->can_move_patch(entry, destination)) {
      continue;
    }
    if (!storage->move_patch(entry, destination)) {
      return false;
    }
    changed();
    return true;
  }
  return false;
}

namespace {

/// A bank's instrument or a package's version, which has no file of its own.
bool is_inside_file(const PatchEntry &entry) {
  return !entry.source_relative_path.empty() ||
         !entry.container_item_id.empty();
}

/// Whether `path` is `folder` or lies inside it.
bool within(const std::filesystem::path &path,
            const std::filesystem::path &folder) {
  const auto relative =
      path.lexically_normal().lexically_relative(folder.lexically_normal());
  return !relative.empty() && *relative.begin() != "..";
}

/// `stem` + `extension` in `folder`, or the first of "<stem> copy",
/// "<stem> copy 2", ... that the folder does not have yet, ignoring case as
/// some filesystems do.
std::filesystem::path unused_copy_path(const std::filesystem::path &folder,
                                       const std::string &stem,
                                       const std::string &extension) {
  std::vector<std::string> taken;
  std::error_code ec;
  for (const auto &item : std::filesystem::directory_iterator(folder, ec)) {
    const auto name = item.path().filename();
    if (extension.empty()) {
      taken.push_back(name.string());
    } else if (equals_case_insensitive(name.extension().string(), extension)) {
      taken.push_back(name.stem().string());
    }
  }
  const std::string free_stem =
      unused_folder_name(taken, stem) == stem
          ? stem
          : unused_folder_name(taken, duplicate_stem(stem));
  return folder / (free_stem + extension);
}

/// `source`, a file with its sidecars or a folder with everything in it,
/// copied into `folder` under its own name or, taken, a name of its own.
std::filesystem::path copy_path_into(const std::filesystem::path &source,
                                     const std::filesystem::path &folder) {
  std::error_code ec;
  const bool directory = std::filesystem::is_directory(source, ec);
  const auto target =
      directory ? unused_copy_path(folder, source.filename().string(), "")
                : unused_copy_path(folder, source.stem().string(),
                                   source.extension().string());
  if (directory) {
    // A folder's sidecars are inside it and go with it.
    std::filesystem::copy(source, target,
                          std::filesystem::copy_options::recursive, ec);
  } else if (!std::filesystem::copy_file(source, target, ec) || ec) {
    return {};
  } else {
    sidecar::copy(source, target);
  }
  if (ec) {
    std::filesystem::remove_all(target, ec);
    return {};
  }
  return target;
}

/// The files of `source` a format reads, with their sidecars, copied into
/// `target` with the folders they are in. How many, none leaving no
/// `target` behind.
/// Calls `visit` with each file in `folder` and the folders inside it, past
/// hidden ones: the metadata's, and the system's such as .DS_Store. Stops
/// when `visit` returns false, and then returns false.
template <typename Visit>
bool for_each_visible_file(const std::filesystem::path &folder, Visit &&visit) {
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(folder, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (it->path().filename().string().starts_with(".")) {
      if (it->is_directory(ec)) {
        it.disable_recursion_pending();
      }
      continue;
    }
    if (it->is_regular_file(ec) && !visit(it->path())) {
      return false;
    }
  }
  return true;
}

bool is_readable(const std::filesystem::path &file) {
  return platform::import_pipeline::supports_extension(
      lowercase_extension(file));
}

/// The files Windows leaves in folders, unseen there.
bool is_system_file(const std::filesystem::path &file) {
  const auto name = file.filename().string();
  return equals_case_insensitive(name, "Thumbs.db") ||
         equals_case_insensitive(name, "desktop.ini");
}

std::size_t copy_readable_tree(const std::filesystem::path &source,
                               const std::filesystem::path &target) {
  std::size_t copied = 0;
  for_each_visible_file(source, [&](const std::filesystem::path &path) {
    if (!is_readable(path)) {
      return true;
    }
    const auto copy = target / path.lexically_relative(source);
    std::error_code ec;
    std::filesystem::create_directories(copy.parent_path(), ec);
    if (ec || !std::filesystem::copy_file(path, copy, ec) || ec) {
      return true;
    }
    sidecar::copy(path, copy);
    ++copied;
    return true;
  });
  if (copied == 0) {
    std::error_code ec;
    std::filesystem::remove_all(target, ec);
  }
  return copied;
}

} // namespace

bool PatchRepository::takes_copies(const PatchEntry &destination) const {
  if (!destination.is_directory || !destination.format.empty() ||
      is_inside_file(destination)) {
    return false;
  }
  const auto *owner = workspace_.owner_of(destination.full_path);
  return owner != nullptr && owner->writable &&
         vfs_.is_directory(destination.full_path);
}

bool PatchRepository::can_copy_patch(const PatchEntry &entry,
                                     const PatchEntry &destination) const {
  if (!takes_copies(destination)) {
    return false;
  }
  // A folder does not go inside itself.
  return is_inside_file(entry) ||
         !within(destination.full_path, entry.full_path);
}

std::optional<std::filesystem::path>
PatchRepository::copy_patch(const PatchEntry &entry,
                            const PatchEntry &destination,
                            std::string_view extension) {
  if (!can_copy_patch(entry, destination)) {
    return std::nullopt;
  }

  std::filesystem::path target;
  if (is_inside_file(entry)) {
    ym2612::Patch patch;
    if (!load_patch(entry, patch)) {
      return std::nullopt;
    }
    target = unused_copy_path(destination.full_path, entry_stem(entry),
                              std::string(extension));
    patch.name = target.stem().string();
    if (!write_patch(patch, target)) {
      std::error_code ec;
      std::filesystem::remove_all(target, ec);
      return std::nullopt;
    }
    copy_patch_metadata(entry.metadata, target);
  } else {
    target = copy_path_into(entry.full_path, destination.full_path);
    if (target.empty()) {
      return std::nullopt;
    }
    // As the tree shows it, a read-only folder's own metadata laid over.
    if (std::error_code ec; !std::filesystem::is_directory(target, ec)) {
      copy_patch_metadata(entry.metadata, target);
    }
  }
  changed();
  return target;
}

std::vector<std::filesystem::path>
PatchRepository::copy_files_in(const std::vector<std::filesystem::path> &files,
                               const PatchEntry &destination,
                               std::size_t &skipped) {
  std::vector<std::filesystem::path> copies;
  skipped = 0;
  for (const auto &file : files) {
    std::error_code ec;
    std::filesystem::path copy;
    if (!takes_copies(destination) || within(destination.full_path, file)) {
      // Nowhere to go, or into itself.
    } else if (std::filesystem::is_directory(file, ec)) {
      // Only what a format reads: the rest would sit in the folder unseen.
      copy =
          unused_copy_path(destination.full_path, file.filename().string(), "");
      if (copy_readable_tree(file, copy) == 0) {
        copy.clear();
      }
    } else if (is_readable(file)) {
      copy = copy_path_into(file, destination.full_path);
    }
    if (copy.empty()) {
      ++skipped;
    } else {
      copies.push_back(std::move(copy));
    }
  }
  if (!copies.empty()) {
    changed();
  }
  return copies;
}

bool PatchRepository::remove_copies(
    const std::vector<std::filesystem::path> &copies) {
  bool removed = true;
  for (const auto &copy : copies) {
    const auto *owner = workspace_.owner_of(copy.parent_path());
    if (owner == nullptr || !owner->writable) {
      removed = false;
      continue;
    }
    std::error_code ec;
    if (std::filesystem::is_directory(copy, ec)) {
      // Its sidecars are inside it and go with it.
      std::filesystem::remove_all(copy, ec);
    } else if (std::filesystem::remove(copy, ec) && !ec) {
      sidecar::remove(copy);
    }
    removed = removed && !ec;
  }
  changed();
  return removed;
}

std::optional<DeletedEntry>
PatchRepository::keep_for_restore(const PatchEntry &entry,
                                  std::uintmax_t limit) const {
  DeletedEntry kept;
  kept.path = entry.full_path;
  std::error_code ec;
  std::vector<std::filesystem::path> files;
  if (std::filesystem::is_directory(entry.full_path, ec)) {
    kept.is_directory = true;
    // A folder's sidecars are inside it.
    for (std::filesystem::recursive_directory_iterator it(entry.full_path, ec),
         end;
         !ec && it != end; it.increment(ec)) {
      if (it->is_directory(ec)) {
        kept.folders.push_back(it->path());
      } else {
        files.push_back(it->path());
      }
    }
  } else {
    files = sidecar::files_of(entry.full_path);
    files.insert(files.begin(), entry.full_path);
  }
  if (ec) {
    return std::nullopt;
  }

  // Sized up first, so a folder too large to keep is not read at all.
  std::uintmax_t total = 0;
  for (const auto &file : files) {
    std::uintmax_t size = 0;
    if (!vfs_.file_size(file, size) || (total += size) > limit) {
      return std::nullopt;
    }
  }
  for (const auto &file : files) {
    const auto in = vfs_.open_read(file);
    if (!in) {
      return std::nullopt;
    }
    kept.files.push_back({file, std::string(std::istreambuf_iterator<char>(*in),
                                            std::istreambuf_iterator<char>())});
  }
  return kept;
}

std::optional<DeletedEntry>
PatchRepository::trash_patch(const PatchEntry &entry) {
  if (!platform::trash::available() || !can_delete_patch(entry)) {
    return std::nullopt;
  }
  DeletedEntry kept;
  kept.path = entry.full_path;
  kept.is_directory = vfs_.is_directory(entry.full_path);
  auto trashed = platform::trash::move_to_trash(entry.full_path);
  if (!trashed) {
    return std::nullopt;
  }
  kept.trashed = std::move(*trashed);
  // A folder's sidecars are inside it and went with it. A file's follow it,
  // or stay where they are, still its, if they cannot.
  if (!kept.is_directory) {
    for (auto &path : sidecar::roots_of(entry.full_path)) {
      if (auto in_trash = platform::trash::move_to_trash(path)) {
        kept.trashed_sidecars.push_back(
            {std::move(path), std::move(*in_trash)});
      }
    }
    sidecar::prune_folders_of(entry.full_path);
  }
  changed();
  return kept;
}

bool PatchRepository::restore(const DeletedEntry &deleted) {
  const auto *owner = workspace_.owner_of(deleted.path.parent_path());
  if (owner == nullptr || !owner->writable ||
      !vfs_.is_directory(deleted.path.parent_path())) {
    return false;
  }
  if (!deleted.trashed.empty()) {
    return restore_from_trash(deleted);
  }
  if (vfs_.exists(deleted.path)) {
    return false;
  }
  bool written = !deleted.is_directory || vfs_.create_directories(deleted.path);
  for (const auto &folder : deleted.folders) {
    written = written && vfs_.create_directories(folder);
  }
  for (const auto &file : deleted.files) {
    if (!written) {
      break;
    }
    const auto out = vfs_.open_write(file.path);
    // Flushed here, where a write the disk has no room for can be seen.
    written = out &&
              out->write(file.bytes.data(),
                         static_cast<std::streamsize>(file.bytes.size())) &&
              out->flush();
  }
  if (!written) {
    std::error_code ec;
    std::filesystem::remove_all(deleted.path, ec);
    return false;
  }
  changed();
  return true;
}

bool PatchRepository::restore_from_trash(const DeletedEntry &deleted) {
  std::error_code ec;
  // Put back already, as the Finder does, it needs only its sidecars.
  const bool put_back = !std::filesystem::exists(deleted.trashed, ec) &&
                        vfs_.exists(deleted.path);
  if (!put_back) {
    if (vfs_.exists(deleted.path)) {
      return false;
    }
    std::filesystem::rename(deleted.trashed, deleted.path, ec);
    if (ec) {
      return false;
    }
  }
  // The file is back whether or not its metadata can follow.
  for (const auto &sidecar : deleted.trashed_sidecars) {
    if (std::filesystem::exists(sidecar.path, ec)) {
      continue;
    }
    std::filesystem::create_directories(sidecar.path.parent_path(), ec);
    std::filesystem::rename(sidecar.in_trash, sidecar.path, ec);
  }
  changed();
  return true;
}

std::optional<std::size_t>
PatchRepository::unlisted_files(const PatchEntry &folder,
                                std::size_t limit) const {
  std::size_t seen = 0;
  std::size_t unlisted = 0;
  const bool all_seen = for_each_visible_file(
      folder.full_path, [&](const std::filesystem::path &file) {
        if (!is_readable(file) && !is_system_file(file)) {
          ++unlisted;
        }
        return ++seen < limit;
      });
  if (!all_seen) {
    return std::nullopt;
  }
  return unlisted;
}

void PatchRepository::changed() {
  refresh();
#if defined(MEGATOY_PLATFORM_WEB)
  platform::web::request_storage_persist();
#endif
}

bool PatchRepository::can_edit_metadata(const PatchEntry &entry) const {
  return std::any_of(
      storages_.begin(), storages_.end(),
      [&](const auto &storage) { return storage->can_edit_metadata(entry); });
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

std::string PatchRepository::display_path(const std::string &relative) const {
  for (const auto &root : tree_cache_) {
    if (relative == root.relative_path) {
      return root.telling_parents + root.name;
    }
    if (relative.starts_with(root.relative_path + "/")) {
      return root.telling_parents + root.name +
             relative.substr(root.relative_path.size());
    }
  }
  return relative;
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

bool PatchRepository::update_patch_metadata(const std::string &relative_path,
                                            const PatchMetadata &metadata) {
  for (const auto &storage : storages_) {
    if (storage->update_patch_metadata(relative_path, metadata)) {
#if defined(MEGATOY_PLATFORM_WEB)
      // Stars and icons write a sidecar like any other file, so they
      // need the same flush any other write does.
      platform::web::request_storage_persist();
#endif
      return true;
    }
  }
  return false;
}

void PatchRepository::copy_patch_metadata(
    const std::optional<PatchMetadata> &source,
    const std::filesystem::path &file) {
  for (const auto &storage : storages_) {
    if (storage->copy_metadata_to(source, file)) {
      return;
    }
  }
}

namespace {

const PatchEntry *find_listed(const std::vector<PatchEntry> &entries,
                              const std::filesystem::path &path) {
  for (const auto &entry : entries) {
    if (entry.full_path == path && !is_inside_file(entry)) {
      return &entry;
    }
    if (entry.is_directory) {
      if (const auto *found = find_listed(entry.children, path)) {
        return found;
      }
    }
  }
  return nullptr;
}

} // namespace

const PatchEntry *
PatchRepository::find_by_full_path(const std::filesystem::path &path) const {
  return find_listed(tree(), path.lexically_normal());
}

const PatchEntry *
PatchRepository::find_entry(const std::string &relative_path) const {
  if (relative_path.empty()) {
    return nullptr;
  }
  if (const auto it = by_path_.find(relative_path); it != by_path_.end()) {
    return it->second;
  }
  const auto it = by_source_path_.find(relative_path);
  return it == by_source_path_.end() ? nullptr : it->second;
}

} // namespace patches
