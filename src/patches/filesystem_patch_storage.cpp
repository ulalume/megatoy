#include "patches/filesystem_patch_storage.hpp"

#include "formats/ctrmml.hpp"
#include "formats/patch_loader.hpp"
#include "patch_metadata.hpp"
#include "patch_repository.hpp"
#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace {

std::vector<std::string> supported_extensions() {
  return {".gin", ".ginpkg", ".rym2612", ".dmp", ".fui", ".mml"};
}

} // namespace

namespace patches {

FilesystemPatchStorage::FilesystemPatchStorage(
    platform::VirtualFileSystem &vfs, std::filesystem::path root,
    std::string relative_root_label, PatchMetadataManager *metadata_manager)
    : vfs_(vfs), root_(std::move(root)),
      root_label_(std::move(relative_root_label)),
      metadata_manager_(metadata_manager) {}

void FilesystemPatchStorage::append_entries(
    std::vector<PatchEntry> &tree) const {
  if (!vfs_.is_directory(root_)) {
    return;
  }

  std::vector<PatchEntry> children;
  scan_directory(root_, children, root_label_);
  if (children.empty()) {
    return;
  }

  if (!root_label_.empty()) {
    PatchEntry root_entry;
    root_entry.name = root_label_;
    root_entry.relative_path = root_label_;
    root_entry.full_path = root_;
    root_entry.is_directory = true;
    root_entry.children = std::move(children);
    tree.push_back(std::move(root_entry));
  } else {
    tree.insert(tree.end(), children.begin(), children.end());
  }
}

bool FilesystemPatchStorage::load_patch(const PatchEntry &entry,
                                        ym2612::Patch &out_patch) const {
  if (entry.is_directory) {
    return false;
  }
  auto result = formats::load_patch_from_file(entry.full_path);
  if (result.status == formats::PatchLoadStatus::Failure) {
    return false;
  } else if (result.status == formats::PatchLoadStatus::Success) {
    out_patch = result.patches[0];
    return true;
  }
  size_t instrument_index = entry.ctrmml_index;
  if (instrument_index < result.patches.size()) {
    out_patch = result.patches[instrument_index];
    return true;
  }
  return false;
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

      if (extension == ".mml") {
        std::vector<ym2612::Patch> instruments =
            formats::ctrmml::read_file(path);
        if (!instruments.empty()) {
          PatchEntry container;
          container.name = path.stem().string();
          container.full_path = path;
          container.relative_path = info.relative_path;
          container.is_directory = true;
          container.format = "ctrmml";

          for (size_t idx = 0; idx < instruments.size(); ++idx) {
            const auto &instrument = instruments[idx];
            PatchEntry child;
            child.name = instrument.name;
            child.full_path = path;
            std::string identifier = instrument.name;
            std::replace(identifier.begin(), identifier.end(), '/', '_');
            std::replace(identifier.begin(), identifier.end(), '\\', '_');
            child.relative_path = container.relative_path + "/" +
                                  std::to_string(idx) + "_" + identifier;
            child.format = "ctrmml";
            child.is_directory = false;
            child.children.clear();
            child.ctrmml_index = idx;

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

  static const std::unordered_map<std::string, std::string> format_map = {
      {".gin", "gin"}, {".ginpkg", "ginpkg"}, {".rym2612", "rym2612"},
      {".dmp", "dmp"}, {".fui", "fui"},       {".mml", "ctrmml"}};

  auto it = format_map.find(extension);
  return it != format_map.end() ? it->second : "unknown";
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
  if (!metadata_manager_ || entry.is_directory) {
    return;
  }
  entry.metadata = metadata_manager_->get_metadata(entry.relative_path);
}

} // namespace patches
