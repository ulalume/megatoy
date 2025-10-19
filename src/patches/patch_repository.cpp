#include "patch_repository.hpp"
#include "formats/ctrmml.hpp"

#include "formats/patch_loader.hpp"
#include "ym2612/patch.hpp"
#include <algorithm>
#include <iostream>
#include <unordered_map>

namespace patches {

PatchRepository::PatchRepository(const std::filesystem::path &patches_root,
                                 const std::filesystem::path &builtin_dir)
    : patches_directory_(patches_root), builtin_patch_directory_(builtin_dir),
      has_builtin_directory_(!builtin_dir.empty()), cache_initialized_(false) {
  refresh();
}

void PatchRepository::refresh() {
  tree_cache_.clear();

  try {
    if (std::filesystem::exists(patches_directory_) &&
        std::filesystem::is_directory(patches_directory_)) {
      user_time_valid_ = true;
      last_user_directory_check_time_ =
          std::filesystem::last_write_time(patches_directory_);
      scan_directory(patches_directory_, tree_cache_);
    } else {
      user_time_valid_ = false;
    }

    if (has_builtin_directory_ &&
        std::filesystem::exists(builtin_patch_directory_) &&
        std::filesystem::is_directory(builtin_patch_directory_)) {
      PatchEntry builtin_root;
      builtin_root.name = kBuiltinRootName;
      builtin_root.relative_path = kBuiltinRootName;
      builtin_root.full_path = builtin_patch_directory_;
      builtin_root.format = "";
      builtin_root.is_directory = true;

      scan_directory(builtin_patch_directory_, builtin_root.children,
                     builtin_root.relative_path);

      if (!builtin_root.children.empty()) {
        tree_cache_.push_back(std::move(builtin_root));
      }

      builtin_time_valid_ = true;
      last_builtin_directory_check_time_ =
          std::filesystem::last_write_time(builtin_patch_directory_);
    } else {
      builtin_time_valid_ = false;
    }

    cache_initialized_ = true;
  } catch (const std::filesystem::filesystem_error &e) {
    std::cerr << "Error scanning preset directories: " << e.what() << std::endl;
    cache_initialized_ = false;
  }
}

const std::vector<PatchEntry> &PatchRepository::tree() const {
  return tree_cache_;
}

bool PatchRepository::load_patch(const PatchEntry &entry,
                                 ym2612::Patch &patch) const {
  if (entry.is_directory) {
    return false;
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
    size_t instrument_index = 0;
    if (!entry.metadata.empty()) {
      try {
        instrument_index = static_cast<size_t>(std::stoul(entry.metadata));
      } catch (...) {
        instrument_index = 0;
      }
    }
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

  try {
    if (std::filesystem::exists(patches_directory_) &&
        std::filesystem::is_directory(patches_directory_)) {
      auto current_time = std::filesystem::last_write_time(patches_directory_);
      if (!user_time_valid_ ||
          current_time != last_user_directory_check_time_) {
        changed = true;
      }
    } else if (user_time_valid_) {
      changed = true;
    }
  } catch (const std::filesystem::filesystem_error &) {
    changed = true;
  }

  try {
    if (has_builtin_directory_ &&
        std::filesystem::exists(builtin_patch_directory_) &&
        std::filesystem::is_directory(builtin_patch_directory_)) {
      auto current_time =
          std::filesystem::last_write_time(builtin_patch_directory_);
      if (!builtin_time_valid_ ||
          current_time != last_builtin_directory_check_time_) {
        changed = true;
      }
    } else if (builtin_time_valid_) {
      changed = true;
    }
  } catch (const std::filesystem::filesystem_error &) {
    changed = true;
  }

  return changed;
}

std::vector<std::string> PatchRepository::supported_extensions() {
  return {".gin", ".rym2612", ".dmp", ".fui", ".mml"};
}

void PatchRepository::scan_directory(const std::filesystem::path &dir_path,
                                     std::vector<PatchEntry> &tree,
                                     const std::string &relative_path) {
  if (!std::filesystem::exists(dir_path) ||
      !std::filesystem::is_directory(dir_path)) {
    return;
  }

  std::vector<std::filesystem::directory_entry> entries;

  try {
    for (const auto &entry : std::filesystem::directory_iterator(dir_path)) {
      entries.push_back(entry);
    }
  } catch (const std::filesystem::filesystem_error &e) {
    std::cerr << "Error reading directory " << dir_path << ": " << e.what()
              << std::endl;
    return;
  }

  std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
    if (a.is_directory() != b.is_directory()) {
      return a.is_directory();
    }
    return a.path().filename().string() < b.path().filename().string();
  });

  for (const auto &entry : entries) {
    const auto &path = entry.path();
    std::string filename = path.filename().string();

    if (filename.starts_with(".")) {
      continue;
    }

    PatchEntry info;
    info.name = filename;
    info.full_path = path;
    info.relative_path =
        relative_path.empty() ? filename : relative_path + "/" + filename;
    info.metadata = "";

    if (entry.is_directory()) {
      info.is_directory = true;
      info.format = "";
      scan_directory(path, info.children, info.relative_path);
      if (!info.children.empty()) {
        tree.push_back(std::move(info));
      }
    } else if (entry.is_regular_file()) {
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
          container.metadata = "";
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
            child.metadata = std::to_string(idx);
            child.format = "ctrmml";
            child.is_directory = false;
            child.children.clear();
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

      tree.push_back(std::move(info));
    }
  }
}

std::string
PatchRepository::detect_format(const std::filesystem::path &file_path) const {
  std::string extension = file_path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 ::tolower);

  static const std::unordered_map<std::string, std::string> format_map = {
      {".gin", "gin"},
      {".rym2612", "rym2612"},
      {".dmp", "dmp"},
      {".fui", "fui"},
      {".mml", "ctrmml"}};

  auto it = format_map.find(extension);
  return it != format_map.end() ? it->second : "unknown";
}

bool PatchRepository::is_supported_file(
    const std::filesystem::path &file_path) const {
  std::string extension = file_path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 ::tolower);

  auto supported = supported_extensions();
  return std::find(supported.begin(), supported.end(), extension) !=
         supported.end();
}

std::filesystem::path
PatchRepository::to_relative_path(const std::filesystem::path &path) const {
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

} // namespace patches
