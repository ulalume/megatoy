#include "patch_manager.hpp"
#include "../formats/ctrmml.hpp"
#include "../formats/dmp.hpp"
#include "../formats/gin.hpp"
#include "../platform/file_dialog.hpp"

#include <iostream>
#include <utility>

namespace patches {

PatchManager::PatchManager(megatoy::system::DirectoryService &directories)
    : directories_(directories),
      repository_(directories_.paths().patches_root,
                  directories_.paths().builtin_presets_root) {}

ym2612::Patch &PatchManager::current_patch() { return current_patch_; }

const ym2612::Patch &PatchManager::current_patch() const {
  return current_patch_;
}

const std::string &PatchManager::current_patch_path() const {
  return current_patch_path_;
}

void PatchManager::set_current_patch_path(const std::filesystem::path &path) {
  if (path.empty()) {
    current_patch_path_.clear();
  } else {
    current_patch_path_ = path.generic_string();
  }
}

bool PatchManager::load_patch(const PatchEntry &entry) {
  ym2612::Patch loaded_patch;
  if (!repository_.load_patch(entry, loaded_patch)) {
    return false;
  }

  current_patch_ = loaded_patch;
  current_patch_path_ = entry.relative_path;
  return true;
}

void PatchManager::refresh_directories() {
  repository_ = PatchRepository(directories_.paths().patches_root,
                                directories_.paths().builtin_presets_root);
}

PatchRepository &PatchManager::repository() { return repository_; }
const PatchRepository &PatchManager::repository() const { return repository_; }

SaveResult PatchManager::save_current_patch(bool force_overwrite = false) {
  // Check whether the file already exists
  auto patches_dir = directories_.paths().user_patches_root;
  auto patch_path =
      ym2612::formats::gin::build_patch_path(patches_dir, current_patch_.name);
  if (std::filesystem::exists(patch_path) && !force_overwrite) {
    return SaveResult::duplicated();
  } else {
    // Save as new file
    if (ym2612::formats::gin::save_patch(patches_dir, current_patch_,
                                         current_patch_.name)
            .has_value()) {
      return SaveResult::success(patch_path);
    } else {
      return SaveResult::error("Please check directory permissions.");
    }
  }
}

SaveResult PatchManager::export_current_patch_as(ExportFormat format) {
  const auto &default_dir = directories_.paths().export_root;
  const std::string sanitized_name = sanitize_filename(
      current_patch_.name.empty() ? "patch" : current_patch_.name);

  switch (format) {
  case ExportFormat::DMP: {
    std::filesystem::path selected_path;
    std::string default_filename =
        sanitized_name.empty() ? "patch.dmp" : sanitized_name + ".dmp";

    auto result = platform::file_dialog::save_file(
        default_dir, default_filename, {{"DefleMask Preset", {"dmp"}}},
        selected_path);

    if (result == platform::file_dialog::DialogResult::Ok) {
      if (selected_path.extension().empty()) {
        selected_path.replace_extension(".dmp");
      }

      if (ym2612::formats::dmp::write_patch(current_patch_, selected_path)) {
        return SaveResult::success(selected_path);
      } else {
        return SaveResult::error("Failed to export DMP file: " +
                                 selected_path.string());
      }
    } else if (result == platform::file_dialog::DialogResult::Cancelled) {
      return SaveResult::cancelled();
    } else {
      return SaveResult::error("Could not open save dialog for DMP export");
    }
  }

  case ExportFormat::MML: {
    std::filesystem::path selected_path;
    std::string default_filename =
        sanitized_name.empty() ? "patch.mml" : sanitized_name + ".mml";

    auto result = platform::file_dialog::save_file(
        default_dir, default_filename,
        {{"ctrmml text", {"txt"}}, {"MML", {"mml"}}}, selected_path);

    if (result == platform::file_dialog::DialogResult::Ok) {
      if (selected_path.extension().empty()) {
        selected_path.replace_extension(".mml");
      }

      if (ym2612::formats::ctrmml::write_patch(current_patch_, selected_path)) {
        return SaveResult::success(selected_path);
      } else {
        return SaveResult::error("Failed to export MML file: " +
                                 selected_path.string());
      }
    } else if (result == platform::file_dialog::DialogResult::Cancelled) {
      return SaveResult::cancelled();
    } else {
      return SaveResult::error("Could not open save dialog for MML export");
    }
  }

  default:
    return SaveResult::error("Unknown export format");
  }
}

} // namespace patches
