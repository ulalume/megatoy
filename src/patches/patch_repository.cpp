#include "patch_repository.hpp"
#include "../parsers/ctrmml_parser.hpp"
#include "../parsers/dmp_parser.hpp"
#include "../parsers/fui_parser.hpp"
#include "../parsers/rym2612_parser.hpp"
#include "../ym2612/patch_io.hpp"
#include <algorithm>
#include <iostream>

namespace patches {

PatchRepository::PatchRepository(const std::filesystem::path &preset_dir)
    : patch_directory_(preset_dir), cache_initialized_(false) {
  refresh();
}

void PatchRepository::refresh() {
  tree_cache_.clear();

  if (!std::filesystem::exists(patch_directory_)) {
    std::cerr << "Preset directory does not exist: " << patch_directory_
              << std::endl;
    cache_initialized_ = true;
    return;
  }

  try {
    last_directory_check_time_ =
        std::filesystem::last_write_time(patch_directory_);
    scan_directory(patch_directory_, tree_cache_);
    cache_initialized_ = true;
  } catch (const std::filesystem::filesystem_error &e) {
    std::cerr << "Error scanning preset directory: " << e.what() << std::endl;
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

  try {
    if (entry.format == "rym2612") {
      return parsers::parse_rym2612_file(entry.full_path, patch);
    } else if (entry.format == "gin") {
      auto patches_dir = entry.full_path.parent_path();
      auto filename = entry.full_path.stem().string();
      return ym2612::load_patch(patches_dir, patch, filename);
    } else if (entry.format == "dmp") {
      return parsers::parse_dmp_file(entry.full_path, patch);
    } else if (entry.format == "fui") {
      return parsers::parse_fui_file(entry.full_path, patch);
    } else if (entry.format == "ctrmml") {
      std::vector<parsers::CtrmmlInstrument> instruments;
      if (!parsers::parse_ctrmml_file(entry.full_path, instruments)) {
        return false;
      }
      size_t instrument_index = 0;
      if (!entry.metadata.empty()) {
        try {
          instrument_index = static_cast<size_t>(std::stoul(entry.metadata));
        } catch (...) {
          instrument_index = 0;
        }
      }
      if (instrument_index >= instruments.size()) {
        return false;
      }
      patch = instruments[instrument_index].patch;
      return true;
    }
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

  if (!std::filesystem::exists(patch_directory_)) {
    return false;
  }

  try {
    auto current_time = std::filesystem::last_write_time(patch_directory_);
    return current_time != last_directory_check_time_;
  } catch (const std::filesystem::filesystem_error &) {
    return true;
  }
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
        std::vector<parsers::CtrmmlInstrument> instruments;
        if (parsers::parse_ctrmml_file(path, instruments) &&
            !instruments.empty()) {
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

      if (info.format == "gin") {
        info.name = path.stem().string();
      } else if (info.format == "rym2612") {
        info.name = parsers::get_rym2612_patch_name(path);
      } else if (info.format == "dmp") {
        info.name = parsers::get_dmp_patch_name(path);
      } else if (info.format == "fui") {
        info.name = path.stem().string();
      }

      tree.push_back(std::move(info));
    }
  }
}

std::string
PatchRepository::detect_format(const std::filesystem::path &file_path) const {
  std::string extension = file_path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 ::tolower);

  if (extension == ".gin") {
    return "gin";
  } else if (extension == ".rym2612") {
    return "rym2612";
  } else if (extension == ".dmp") {
    return "dmp";
  } else if (extension == ".fui") {
    return "fui";
  } else if (extension == ".mml") {
    return "ctrmml";
  }

  return "unknown";
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
  return path.lexically_relative(patch_directory_);
}

std::filesystem::path
PatchRepository::to_absolute_path(const std::filesystem::path &path) const {
  return patch_directory_ / path;
}

} // namespace patches
