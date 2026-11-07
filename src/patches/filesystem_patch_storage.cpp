#include "patches/filesystem_patch_storage.hpp"

#include "formats/ginpkg.hpp"
#include "formats/patch_loader.hpp"
#include "formats/patch_registry.hpp"
#include "formats/ym2612_format_adapter.hpp"
#include "patch_repository.hpp"
#include "patches/filename_utils.hpp"
#include "patches/patch_write.hpp"
#include "patches/persistent_parse_cache.hpp"
#include "platform/import_pipeline.hpp"
#include <algorithm>
#include <cctype>
#include <system_error>
#include <unordered_map>

namespace patches {

FilesystemPatchStorage::FilesystemPatchStorage(
    platform::StdFileSystem &vfs, std::filesystem::path root,
    std::string relative_root_label, bool writable,
    PersistentParseCache *persistent_cache,
    std::filesystem::path metadata_overlay_file)
    : vfs_(vfs), root_(std::move(root)),
      root_label_(std::move(relative_root_label)), writable_(writable),
      label_(root_label_), persistent_cache_(persistent_cache) {
  if (label_.empty()) {
    label_ = root_.filename().generic_string();
    root_label_ = label_;
  }
  if (!writable_ && !metadata_overlay_file.empty()) {
    overlay_ =
        std::make_unique<MetadataOverlay>(std::move(metadata_overlay_file));
  }
}

std::string
FilesystemPatchStorage::metadata_key(const std::string &relative_path) const {
  const std::string prefix = root_label_ + "/";
  if (relative_path.rfind(prefix, 0) == 0) {
    return relative_path.substr(prefix.size());
  }
  return relative_path;
}

bool FilesystemPatchStorage::owns_relative_path(
    const std::string &relative_path) const {
  return relative_path == root_label_ ||
         relative_path.rfind(root_label_ + "/", 0) == 0;
}

void FilesystemPatchStorage::set_scan_observer(ScanObserver observer) {
  scan_observer_ = std::move(observer);
}

void FilesystemPatchStorage::append_entries(
    std::vector<PatchEntry> &tree) const {
  seen_container_paths_.clear();
  scan_aborted_ = false;

  PatchEntry root_entry;
  root_entry.name = listed_name_.empty() ? label_ : listed_name_;
  root_entry.telling_parents = listed_parents_;
  root_entry.relative_path = root_label_;
  root_entry.full_path = root_;
  root_entry.is_directory = true;

  // An added folder is listed even when it is empty or missing, so the user
  // can see it is part of the workspace and remove it again.
  if (vfs_.is_directory(root_)) {
    root_entry.holds_nothing =
        scan_directory(root_, root_entry.children, root_label_);
  }
  // An aborted walk never reached the rest of the tree, so its record of what
  // is still on disk is not one to evict from.
  for (auto it = parse_cache_.begin();
       !scan_aborted_ && it != parse_cache_.end();) {
    if (!seen_container_paths_.contains(it->first)) {
      it = parse_cache_.erase(it);
    } else {
      ++it;
    }
  }
  tree.push_back(std::move(root_entry));
}

bool FilesystemPatchStorage::load_patch(const PatchEntry &entry,
                                        ym2612::Patch &out_patch) const {
  if (entry.is_directory || !owns_relative_path(entry.relative_path)) {
    return false;
  }
  if (!entry.container_item_id.empty() &&
      lowercase_extension(entry.full_path) == ".ginpkg") {
    if (entry.container_item_id == "__current__") {
      auto patches = formats::ginpkg::read_file(entry.full_path);
      if (patches.size() == 1) {
        out_patch = std::move(patches.front());
        return true;
      }
      return false;
    }
    auto patch =
        formats::ginpkg::read_version(entry.full_path, entry.container_item_id);
    if (!patch) {
      return false;
    }
    out_patch = std::move(*patch);
    return true;
  }
  auto result = formats::load_patch_from_file(entry.full_path);
  if (result.status == formats::PatchLoadStatus::Failure) {
    return false;
  } else if (result.status == formats::PatchLoadStatus::Success) {
    out_patch = result.patches[0];
    return true;
  }
  size_t instrument_index = entry.instrument_index;
  if (instrument_index < result.patches.size()) {
    out_patch = result.patches[instrument_index];
    return true;
  }
  return false;
}

SavePatchResult
FilesystemPatchStorage::save_patch(const ym2612::Patch &patch,
                                   const std::string &name, bool overwrite,
                                   std::string_view preferred_extension) {
  if (!writable_) {
    return SavePatchResult::unsupported();
  }

  const auto patches_dir = root_;
  const auto sanitized = sanitize_filename(name.empty() ? "patch" : name);
  auto patch_to_write = patch;
  patch_to_write.name = sanitized;

  std::string pref_ext(preferred_extension);
  std::transform(
      pref_ext.begin(), pref_ext.end(), pref_ext.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (pref_ext.empty()) {
    pref_ext = ".dmp";
  }
  if (pref_ext.front() != '.') {
    pref_ext.insert(pref_ext.begin(), '.');
  }

  if (pref_ext != ".ginpkg") {
    const auto target = patches_dir / (sanitized + pref_ext);
    if (!overwrite && vfs_.exists(target)) {
      return SavePatchResult::duplicate();
    }
    return patches::write_patch(patch_to_write, target)
               ? SavePatchResult::success(target)
               : SavePatchResult::error("Failed to save patch");
  }

  const auto ginpkg_path =
      formats::ginpkg::build_package_path(patches_dir, sanitized);
  if (!overwrite && vfs_.exists(ginpkg_path)) {
    return SavePatchResult::duplicate();
  }
  if (auto path = formats::PatchRegistry::instance().save_package(
          ".ginpkg", patches_dir, sanitized, patch_to_write)) {
    return SavePatchResult::success(*path);
  }
  return SavePatchResult::error("Failed to save patch");
}

bool FilesystemPatchStorage::can_rename_patch(const PatchEntry &entry) const {
  if (!writable_ || !owns_relative_path(entry.relative_path) ||
      !entry.source_relative_path.empty() || !entry.container_item_id.empty() ||
      entry.full_path.empty()) {
    return false;
  }

  return maps_to_own_path(entry);
}

bool FilesystemPatchStorage::maps_to_own_path(const PatchEntry &entry) const {
  const auto mapped = to_absolute_path(entry.relative_path);
  return mapped &&
         mapped->lexically_normal() == entry.full_path.lexically_normal();
}

bool FilesystemPatchStorage::can_move_patch(
    const PatchEntry &entry, const PatchEntry &destination) const {
  if (!can_rename_patch(entry) || entry.relative_path == root_label_) {
    return false;
  }
  // Only a plain folder takes entries; a bank or package lists its
  // instruments like one but is a file.
  if (!destination.is_directory || !destination.format.empty() ||
      !destination.source_relative_path.empty() ||
      !destination.container_item_id.empty() ||
      !owns_relative_path(destination.relative_path) ||
      !maps_to_own_path(destination) ||
      !vfs_.is_directory(destination.full_path)) {
    return false;
  }
  if (destination.relative_path == entry.relative_path ||
      destination.relative_path.rfind(entry.relative_path + "/", 0) == 0) {
    return false;
  }
  return std::filesystem::path(entry.relative_path)
             .parent_path()
             .generic_string() != destination.relative_path;
}

bool FilesystemPatchStorage::move_patch(const PatchEntry &entry,
                                        const PatchEntry &destination) {
  if (!can_move_patch(entry, destination)) {
    return false;
  }

  const auto target = destination.full_path / entry.full_path.filename();
  std::error_code ec;
  if (std::filesystem::exists(target, ec) || ec) {
    return false;
  }

  std::filesystem::rename(entry.full_path, target, ec);
  if (ec) {
    return false;
  }

  // A folder's sidecars are inside it and go with it.
  if (std::filesystem::is_regular_file(target, ec) &&
      !sidecar::move(entry.full_path, target)) {
    std::error_code rollback_error;
    std::filesystem::rename(target, entry.full_path, rollback_error);
    return false;
  }
  return true;
}

bool FilesystemPatchStorage::can_delete_patch(const PatchEntry &entry) const {
  // A directory can be renamed but not deleted: deleting one would take
  // everything inside it.
  return can_rename_patch(entry) && !vfs_.is_directory(entry.full_path);
}

bool FilesystemPatchStorage::delete_patch(const PatchEntry &entry) {
  if (!can_delete_patch(entry)) {
    return false;
  }

  std::error_code ec;
  if (!std::filesystem::remove(entry.full_path, ec) || ec) {
    return false;
  }
  sidecar::remove(entry.full_path);
  return true;
}

bool FilesystemPatchStorage::rename_patch(const PatchEntry &entry,
                                          const std::string &new_stem) {
  if (!can_rename_patch(entry) || new_stem.empty() ||
      sanitize_filename(new_stem) != new_stem) {
    return false;
  }

  // A directory's name is its whole name; only a file has an extension to
  // keep. "drums.old" is a folder called that, not one called "drums".
  const bool is_directory = vfs_.is_directory(entry.full_path);
  const auto target =
      entry.full_path.parent_path() /
      (is_directory ? new_stem
                    : new_stem + entry.full_path.extension().string());
  if (target == entry.full_path) {
    return true;
  }

  std::error_code ec;
  if (std::filesystem::exists(target, ec)) {
    if (ec || !std::filesystem::equivalent(entry.full_path, target, ec) || ec) {
      return false;
    }
  } else if (ec) {
    return false;
  }

  std::filesystem::rename(entry.full_path, target, ec);
  if (ec) {
    return false;
  }

  // A folder's sidecars are inside it and go with it.
  if (!is_directory && !sidecar::move(entry.full_path, target)) {
    std::error_code rollback_error;
    std::filesystem::rename(target, entry.full_path, rollback_error);
    return false;
  }
  return true;
}

bool FilesystemPatchStorage::can_edit_metadata(const PatchEntry &entry) const {
  return (writable_ || overlay_) && owns_relative_path(entry.relative_path);
}

std::optional<nlohmann::json>
FilesystemPatchStorage::read_metadata(const std::filesystem::path &sidecar,
                                      const std::string &relative_path) const {
  auto entry = sidecar::read(sidecar);
  if (entry && overlay_) {
    return overlay_->apply(metadata_key(relative_path), std::move(*entry));
  }
  return entry;
}

bool FilesystemPatchStorage::update_patch_metadata(
    const std::string &relative_path, const PatchMetadata &metadata) {
  if ((!writable_ && !overlay_) || !owns_relative_path(relative_path)) {
    return false;
  }
  const auto absolute = to_absolute_path(relative_path);
  if (!absolute) {
    return false;
  }
  const auto path = sidecar::path_for_patch(*absolute, root_);
  const auto base = path.empty() ? std::nullopt : sidecar::read(path);
  if (!base) {
    return false;
  }
  const auto current =
      overlay_ ? overlay_->apply(metadata_key(relative_path), *base) : *base;

  // Fields megatoy does not model are taken as they are now, and the hash
  // and creation time as first recorded.
  const auto existing = metadata_from_json(current);
  PatchMetadata stored = metadata;
  stored.extra = existing.extra;
  if (!existing.hash.empty()) {
    stored.hash = existing.hash;
  }
  const auto now = iso8601_utc_now();
  if (!existing.created_at.empty()) {
    stored.created_at = existing.created_at;
  } else if (stored.created_at.empty()) {
    stored.created_at = now;
  }
  stored.updated_at = now;

  const auto entry = metadata_to_json(stored);
  if (overlay_) {
    return overlay_->put(metadata_key(relative_path), entry, *base);
  }
  return sidecar::write(path, entry);
}

bool FilesystemPatchStorage::copy_metadata_to(
    const std::optional<PatchMetadata> &source,
    const std::filesystem::path &file) {
  if (!writable_ || !to_relative_path(file)) {
    return false;
  }
  PatchMetadata copied = source.value_or(PatchMetadata{});
  copied.hash.clear();
  copied.created_at.clear();
  copied.updated_at.clear();
  auto entry = metadata_to_json(copied);
  if (!holds_nothing_of_note(entry)) {
    const auto now = iso8601_utc_now();
    entry["created_at"] = now;
    entry["updated_at"] = now;
  }
  return sidecar::write(sidecar::path_for(file), entry);
}

std::optional<std::filesystem::path> FilesystemPatchStorage::to_relative_path(
    const std::filesystem::path &path) const {
  auto relative = path.lexically_relative(root_);
  if (!relative.empty() && relative.native()[0] != '.') {
    if (root_label_.empty()) {
      return relative;
    }
    return std::filesystem::path(root_label_) / relative;
  }
  return std::nullopt;
}

std::optional<std::filesystem::path> FilesystemPatchStorage::to_absolute_path(
    const std::filesystem::path &path) const {
  const std::string relative_str = path.generic_string();
  if (root_label_.empty()) {
    if (relative_str.empty() || relative_str[0] == '.') {
      return std::nullopt;
    }
    return root_ / path;
  }

  const std::string root_prefix = root_label_ + "/";
  if (relative_str == root_label_) {
    return root_;
  }
  if (!relative_str.empty() && relative_str.rfind(root_prefix, 0) == 0 &&
      relative_str.size() > root_prefix.size()) {
    std::string without_prefix = relative_str.substr(root_prefix.size());
    return root_ / without_prefix;
  }
  return std::nullopt;
}

bool FilesystemPatchStorage::scan_directory(
    const std::filesystem::path &dir_path, std::vector<PatchEntry> &tree,
    const std::string &relative_path) const {
  if (!vfs_.is_directory(dir_path)) {
    return false;
  }

  auto entries = vfs_.read_directory(dir_path);
  const bool sidecars_here =
      std::any_of(entries.begin(), entries.end(), [](const auto &entry) {
        return entry.is_directory &&
               entry.path.filename() == sidecar::kDirectory;
      });
  const bool holds_nothing =
      std::all_of(entries.begin(), entries.end(), [](const auto &entry) {
        return entry.path.filename().string().starts_with(".");
      });
  std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
    const std::string filename_a = a.path.filename().string();
    const std::string filename_b = b.path.filename().string();
    if (filename_a == "user")
      return true;
    if (filename_a == "presets")
      return false;
    if (filename_b == "user")
      return false;
    if (filename_b == "presets")
      return true;
    return std::lexicographical_compare(
        filename_a.begin(), filename_a.end(), filename_b.begin(),
        filename_b.end(),
        [](char a, char b) { return std::tolower(a) < std::tolower(b); });
  });

  for (const auto &entry : entries) {
    const auto &path = entry.path;
    std::string filename = path.filename().string();

    if (filename.starts_with(".")) {
      continue;
    }

    PatchEntry info;
    info.name = filename;
    info.full_path = path;
    info.relative_path =
        relative_path.empty() ? filename : relative_path + "/" + filename;

    if (entry.is_directory) {
      info.is_directory = true;
      info.format = "";
      info.holds_nothing =
          scan_directory(path, info.children, info.relative_path);
      // An empty folder is kept so a new one can be seen; one holding only
      // files megatoy does not read is not.
      if (!info.children.empty() || info.holds_nothing) {
        tree.push_back(std::move(info));
      }
      if (scan_aborted_) {
        return false;
      }
    } else if (entry.is_regular_file) {
      if (scan_observer_.on_file && !scan_observer_.on_file(path)) {
        scan_aborted_ = true;
        return false;
      }

      std::string extension = path.extension().string();
      std::transform(extension.begin(), extension.end(), extension.begin(),
                     ::tolower);

      const auto format = formats::adapter::format_for_extension(extension);
      const bool is_ginpkg = extension == ".ginpkg";
      const bool is_multi_patch =
          format && formats::adapter::is_multi_patch(*format);
      if (is_ginpkg || is_multi_patch) {
        auto cache_path = path.lexically_normal();
        if (!cache_path.is_absolute()) {
          std::error_code ec;
          const auto absolute = std::filesystem::absolute(cache_path, ec);
          if (!ec) {
            cache_path = absolute.lexically_normal();
          }
        }
        seen_container_paths_.insert(cache_path);

        std::uintmax_t file_size = 0;
        std::filesystem::file_time_type modified{};
        const bool has_identity = vfs_.file_size(path, file_size) &&
                                  vfs_.last_write_time(path, modified);
        const auto cached = parse_cache_.find(cache_path);
        if (has_identity && cached != parse_cache_.end() &&
            cached->second.file_size == file_size &&
            cached->second.modified == modified) {
          auto container = cached->second.subtree;
          load_metadata_for_subtree(container, sidecars_here);
          tree.push_back(std::move(container));
          continue;
        }
        if (cached != parse_cache_.end()) {
          parse_cache_.erase(cached);
        }

        auto warmed =
            platform::import_pipeline::take_warmed_container(cache_path);
        if (!warmed && has_identity && persistent_cache_) {
          auto persistent = persistent_cache_->lookup(cache_path, file_size,
                                                      modified, root_label_);
          if (persistent) {
            parse_cache_.insert_or_assign(
                cache_path, ParseCacheEntry{file_size, modified, *persistent});
            load_metadata_for_subtree(*persistent, sidecars_here);
            tree.push_back(std::move(*persistent));
            continue;
          }
        }
        if (!warmed) {
          ++container_parse_count_;
        }
        std::optional<PatchEntry> parsed_container;
        if (is_ginpkg) {
          auto package = warmed && warmed->package
                             ? warmed->package
                             : formats::ginpkg::load_package(path);
          auto current =
              package ? formats::ginpkg::read_current(*package) : std::nullopt;
          if (package && current) {
            PatchEntry container;
            container.name = path.stem().string();
            container.full_path = path;
            container.relative_path = info.relative_path;
            container.is_directory = true;
            container.format = "ginpkg";

            PatchEntry latest;
            latest.name =
                current->name.empty() ? "Latest" : current->name + " (Latest)";
            latest.full_path = path;
            latest.relative_path = container.relative_path + "/latest";
            latest.source_relative_path = container.relative_path;
            latest.container_item_id = "__current__";
            latest.format = "ginpkg";
            latest.is_directory = false;
            container.children.push_back(std::move(latest));

            for (auto it = package->history().rbegin();
                 it != package->history().rend(); ++it) {
              PatchEntry version;
              auto snapshot = formats::ginpkg::read_version(*package, it->uuid);
              version.name = it->comment && !it->comment->empty()
                                 ? *it->comment
                                 : it->timestamp;
              if (snapshot && !snapshot->name.empty()) {
                version.name += " — " + snapshot->name;
              }
              version.full_path = path;
              version.relative_path =
                  container.relative_path + "/version_" + it->uuid;
              version.source_relative_path = container.relative_path;
              version.container_item_id = it->uuid;
              version.format = "ginpkg";
              version.is_directory = false;
              container.children.push_back(std::move(version));
            }
            parsed_container = std::move(container);
          }
        } else {
          std::vector<ym2612::Patch> instruments =
              warmed && !warmed->instruments.empty()
                  ? warmed->instruments
                  : formats::adapter::read_file(*format, path);
          if (!instruments.empty()) {
            const std::string format_name =
                ym2612_format::format_to_extension(*format);

            PatchEntry container;
            container.name = path.stem().string();
            container.full_path = path;
            container.relative_path = info.relative_path;
            container.is_directory = true;
            container.format = format_name;

            for (size_t idx = 0; idx < instruments.size(); ++idx) {
              const auto &instrument = instruments[idx];
              PatchEntry child;
              child.name =
                  instrument.name.empty()
                      ? path.stem().string() + " " + std::to_string(idx)
                      : instrument.name;
              child.full_path = path;
              std::string identifier = child.name;
              std::replace(identifier.begin(), identifier.end(), '/', '_');
              std::replace(identifier.begin(), identifier.end(), '\\', '_');
              child.relative_path = container.relative_path + "/" +
                                    std::to_string(idx) + "_" + identifier;
              child.source_relative_path = container.relative_path;
              child.format = format_name;
              child.is_directory = false;
              child.instrument_index = idx;
              container.children.push_back(std::move(child));
            }

            parsed_container = std::move(container);
          }
        }

        if (parsed_container) {
          if (has_identity) {
            parse_cache_.insert_or_assign(
                cache_path,
                ParseCacheEntry{file_size, modified, *parsed_container});
            if (persistent_cache_) {
              persistent_cache_->store(cache_path, file_size, modified,
                                       root_label_, *parsed_container);
            }
          }
          load_metadata_for_subtree(*parsed_container, sidecars_here);
          tree.push_back(std::move(*parsed_container));
        }
        continue;
      }

      if (!is_supported_file(path)) {
        continue;
      }

      info.is_directory = false;
      info.format = detect_format(path);
      info.name = path.stem().string();

      load_metadata_for_entry(info, sidecars_here);
      tree.push_back(std::move(info));
    }
  }
  return holds_nothing;
}

std::string
FilesystemPatchStorage::detect_format(const std::filesystem::path &file_path) {
  std::string extension = file_path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 ::tolower);

  // Derived from the format library rather than a local table, so a format
  // added there is labelled here without another edit. A stale copy of this
  // table is how .tfi files ended up listed as "unknown".
  if (extension == ".ginpkg") {
    return "ginpkg";
  }
  if (const auto format = formats::adapter::format_for_extension(extension)) {
    // Historical display name; the extension alone would read as just "mml".
    if (*format == ym2612_format::Format::Mml) {
      return "ctrmml";
    }
    return extension.substr(1);
  }
  return "unknown";
}

bool FilesystemPatchStorage::is_supported_file(
    const std::filesystem::path &file_path) {
  std::string extension = file_path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 ::tolower);

  const auto &supported = platform::import_pipeline::supported_extensions();
  return std::find(supported.begin(), supported.end(), extension) !=
         supported.end();
}

void FilesystemPatchStorage::load_metadata_for_entry(PatchEntry &entry,
                                                     bool sidecars_here) const {
  entry.metadata.reset();
  if (entry.is_directory || (!sidecars_here && !overlay_)) {
    return;
  }
  // An instrument in a bank or package is named by its path below the file.
  std::string item;
  if (!entry.source_relative_path.empty() &&
      entry.relative_path.size() > entry.source_relative_path.size()) {
    item = entry.relative_path.substr(entry.source_relative_path.size() + 1);
  }
  const auto path = sidecars_here ? sidecar::path_for(entry.full_path, item)
                                  : std::filesystem::path{};
  const auto json = read_metadata(path, entry.relative_path);
  if (json && !json->empty()) {
    entry.metadata = metadata_from_json(*json);
  }
}

void FilesystemPatchStorage::load_metadata_for_subtree(
    PatchEntry &entry, bool sidecars_here) const {
  if (entry.is_directory) {
    for (auto &child : entry.children) {
      load_metadata_for_subtree(child, sidecars_here);
    }
    return;
  }
  load_metadata_for_entry(entry, sidecars_here);
}

} // namespace patches
