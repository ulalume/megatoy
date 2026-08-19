#include "patches/filesystem_patch_storage.hpp"

#include "formats/ginpkg.hpp"
#include "formats/patch_loader.hpp"
#include "formats/patch_registry.hpp"
#include "formats/ym2612_format_adapter.hpp"
#include "patch_repository.hpp"
#include "patches/filename_utils.hpp"
#include "patches/patch_write.hpp"
#include <algorithm>
#include <cctype>
#include <system_error>
#include <unordered_map>

namespace {

std::vector<std::string> supported_extensions() {
  auto extensions = formats::adapter::readable_extensions();
  extensions.push_back(".ginpkg");
  return extensions;
}

} // namespace

namespace patches {

FilesystemPatchStorage::FilesystemPatchStorage(platform::VirtualFileSystem &vfs,
                                               std::filesystem::path root,
                                               std::string relative_root_label,
                                               bool writable,
                                               bool enable_metadata)
    : vfs_(vfs), root_(std::move(root)),
      root_label_(std::move(relative_root_label)), writable_(writable),
      label_(root_label_) {
  if (label_.empty()) {
    label_ = root_.filename().generic_string();
    root_label_ = label_;
  }
  if (enable_metadata) {
    metadata_ = std::make_unique<FolderMetadataStore>(root_ / ".megatoy" /
                                                      "patches.json");
    metadata_->load();
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

void FilesystemPatchStorage::append_entries(
    std::vector<PatchEntry> &tree) const {
  PatchEntry root_entry;
  root_entry.name = label_;
  root_entry.relative_path = root_label_;
  root_entry.full_path = root_;
  root_entry.is_directory = true;

  // An added folder is listed even when it is empty or missing, so the user
  // can see it is part of the workspace and remove it again.
  if (vfs_.is_directory(root_)) {
    scan_directory(root_, root_entry.children, root_label_);
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

  const auto ginpkg_path =
      formats::ginpkg::build_package_path(patches_dir, sanitized);
  const auto gin_path = patches_dir / (sanitized + ".gin");

  std::string pref_ext(preferred_extension);
  std::transform(
      pref_ext.begin(), pref_ext.end(), pref_ext.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const bool prefer_ginpkg = pref_ext == ".ginpkg";
  const bool prefer_gin = pref_ext == ".gin";

  if (!pref_ext.empty() && !prefer_ginpkg && !prefer_gin) {
    if (pref_ext.front() != '.') {
      pref_ext.insert(pref_ext.begin(), '.');
    }
    const auto target = patches_dir / (sanitized + pref_ext);
    if (!overwrite && vfs_.exists(target)) {
      return SavePatchResult::duplicate();
    }
    return patches::write_patch(patch, target)
               ? SavePatchResult::success(target)
               : SavePatchResult::error("Failed to save patch");
  }

  const bool ginpkg_exists = vfs_.exists(ginpkg_path);
  const bool gin_exists = vfs_.exists(gin_path);

  auto save_ginpkg = [&]() -> SavePatchResult {
    if (!overwrite && ginpkg_exists) {
      return SavePatchResult::duplicate();
    }
    if (auto path = formats::PatchRegistry::instance().save_package(
            ".ginpkg", patches_dir, sanitized, patch)) {
      return SavePatchResult::success(*path);
    }
    return SavePatchResult::error("Failed to save patch");
  };

  auto save_gin = [&]() -> SavePatchResult {
    if (gin_exists && !overwrite) {
      return SavePatchResult::duplicate();
    }
    if (auto path = formats::PatchRegistry::instance().save_package(
            ".gin", patches_dir, sanitized, patch)) {
      return SavePatchResult::success(*path);
    }
    return SavePatchResult::error("Failed to save patch");
  };

  // Preserve the existing format when possible.
  if (prefer_ginpkg) {
    auto result = save_ginpkg();
    if (result.status != SavePatchResult::Status::Error) {
      return result;
    }
    return save_gin();
  }

  if (prefer_gin) {
    auto result = save_gin();
    if (result.status != SavePatchResult::Status::Error) {
      return result;
    }
    return save_ginpkg();
  }

  if (gin_exists) {
    return save_gin();
  }
  if (ginpkg_exists) {
    return save_ginpkg();
  }

  // GIN is the normal one-patch save format. GINPKG remains available for
  // explicit versioned packages and as a compatibility fallback.
  auto gin_result = save_gin();
  if (gin_result.status == SavePatchResult::Status::Success) {
    return gin_result;
  }
  return save_ginpkg();
}

bool FilesystemPatchStorage::can_delete_patch(const PatchEntry &entry) const {
  if (!writable_ || !owns_relative_path(entry.relative_path) ||
      !entry.source_relative_path.empty() || !entry.container_item_id.empty() ||
      entry.full_path.empty() || vfs_.is_directory(entry.full_path)) {
    return false;
  }

  const auto mapped = to_absolute_path(entry.relative_path);
  return mapped &&
         mapped->lexically_normal() == entry.full_path.lexically_normal();
}

bool FilesystemPatchStorage::delete_patch(const PatchEntry &entry) {
  if (!can_delete_patch(entry)) {
    return false;
  }

  std::error_code ec;
  if (!std::filesystem::remove(entry.full_path, ec) || ec) {
    return false;
  }
  if (metadata_) {
    metadata_->remove(metadata_key(entry.relative_path));
  }
  return true;
}

bool FilesystemPatchStorage::can_edit_metadata(const PatchEntry &entry) const {
  return metadata_ && owns_relative_path(entry.relative_path);
}

bool FilesystemPatchStorage::save_patch_metadata(
    const std::string &relative_path, const ym2612::Patch &patch,
    const PatchMetadata &metadata) {
  if (!metadata_ || !owns_relative_path(relative_path)) {
    return false;
  }

  PatchMetadata stored = metadata;
  stored.path = metadata_key(relative_path);
  stored.hash = patch.hash();
  return metadata_->put(std::move(stored));
}

bool FilesystemPatchStorage::update_patch_metadata(
    const std::string &relative_path, const PatchMetadata &metadata) {
  if (!metadata_ || !owns_relative_path(relative_path)) {
    return false;
  }

  const auto key = metadata_key(relative_path);
  PatchMetadata stored = metadata;
  stored.path = key;
  // Keep the recorded hash: this path only edits ratings and categories, and
  // the caller has no patch to hash.
  if (auto existing = metadata_->get(key)) {
    stored.hash = existing->hash;
  }
  return metadata_->put(std::move(stored));
}

std::optional<PatchMetadata> FilesystemPatchStorage::get_patch_metadata(
    const std::string &relative_path) const {
  if (!metadata_ || !owns_relative_path(relative_path)) {
    return std::nullopt;
  }
  return metadata_->get(metadata_key(relative_path));
}

void FilesystemPatchStorage::cleanup_metadata(
    const std::vector<std::string> &paths) const {
  if (!metadata_) {
    return;
  }
  std::vector<std::string> mine;
  mine.reserve(paths.size());
  for (const auto &path : paths) {
    if (owns_relative_path(path)) {
      mine.push_back(metadata_key(path));
    }
  }
  metadata_->retain_only(mine);
}

std::optional<bool>
FilesystemPatchStorage::has_patch_named(const std::string &name) const {
  if (!writable_) {
    return std::nullopt;
  }
  auto sanitized = sanitize_filename(name.empty() ? "patch" : name);
  const auto patches_dir = root_;
  auto ginpkg_target =
      formats::ginpkg::build_package_path(patches_dir, sanitized);
  auto gin_target = patches_dir / (sanitized + ".gin");
  return vfs_.exists(ginpkg_target) || vfs_.exists(gin_target);
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

void FilesystemPatchStorage::scan_directory(
    const std::filesystem::path &dir_path, std::vector<PatchEntry> &tree,
    const std::string &relative_path) const {
  if (!vfs_.is_directory(dir_path)) {
    return;
  }

  auto entries = vfs_.read_directory(dir_path);
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
      scan_directory(path, info.children, info.relative_path);
      if (!info.children.empty()) {
        tree.push_back(std::move(info));
      }
    } else if (entry.is_regular_file) {
      std::string extension = path.extension().string();
      std::transform(extension.begin(), extension.end(), extension.begin(),
                     ::tolower);

      if (extension == ".ginpkg") {
        auto package = formats::ginpkg::load_package(path);
        auto current = formats::ginpkg::read_file(path);
        if (package && current.size() == 1) {
          PatchEntry container;
          container.name = path.stem().string();
          container.full_path = path;
          container.relative_path = info.relative_path;
          container.is_directory = true;
          container.format = "ginpkg";

          PatchEntry latest;
          latest.name = current.front().name.empty()
                            ? "Latest"
                            : current.front().name + " (Latest)";
          latest.full_path = path;
          latest.relative_path = container.relative_path + "/latest";
          latest.source_relative_path = container.relative_path;
          latest.container_item_id = "__current__";
          latest.format = "ginpkg";
          latest.is_directory = false;
          load_metadata_for_entry(latest);
          container.children.push_back(std::move(latest));

          for (auto it = package->history().rbegin();
               it != package->history().rend(); ++it) {
            PatchEntry version;
            auto snapshot = formats::ginpkg::read_version(path, it->uuid);
            version.name = it->comment && !it->comment->empty() ? *it->comment
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
            load_metadata_for_entry(version);
            container.children.push_back(std::move(version));
          }
          tree.push_back(std::move(container));
        }
        continue;
      }

      // Bank formats (.mml, .dmf, .fur, .opm) hold many instruments, so they
      // are shown as a folder of patches rather than a single entry. Only
      // these are parsed during the scan; single-patch files are left alone
      // so browsing a large library stays cheap.
      const auto format = formats::adapter::format_for_extension(extension);
      if (format && formats::adapter::is_multi_patch(*format)) {
        std::vector<ym2612::Patch> instruments =
            formats::adapter::read_file(*format, path);
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
            child.name = instrument.name.empty()
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
            child.children.clear();
            child.instrument_index = idx;

            load_metadata_for_entry(child);
            container.children.push_back(std::move(child));
          }

          tree.push_back(std::move(container));
        }
        continue;
      }

      if (!is_supported_file(path)) {
        continue;
      }

      info.is_directory = false;
      info.format = detect_format(path);
      info.name = formats::get_patch_name_from_file(path, info.format);

      load_metadata_for_entry(info);
      tree.push_back(std::move(info));
    }
  }
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

  auto supported = supported_extensions();
  return std::find(supported.begin(), supported.end(), extension) !=
         supported.end();
}

void FilesystemPatchStorage::load_metadata_for_entry(PatchEntry &entry) const {
  if (!metadata_ || entry.is_directory) {
    return;
  }
  entry.metadata = metadata_->get(metadata_key(entry.relative_path));
}

} // namespace patches
