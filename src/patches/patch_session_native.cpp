#if !defined(MEGATOY_PLATFORM_WEB)

#include "patch_session.hpp"

#include "formats/ctrmml.hpp"
#include "formats/dmp.hpp"
#include "formats/ginpkg.hpp"
#include "patches/filename_utils.hpp"
#include "platform/file_dialog.hpp"

namespace patches {

SaveResult PatchSession::save_current_patch(bool force_overwrite) {
  auto patches_dir = directories_.paths().user_patches_root;
  auto patch_path =
      formats::ginpkg::build_package_path(patches_dir, current_patch_.name);

  if (std::filesystem::exists(patch_path) && !force_overwrite) {
    return SaveResult::duplicated();
  }

  auto result = formats::ginpkg::save_patch(patches_dir, current_patch_,
                                            current_patch_.name);
  if (result.has_value()) {
    mark_as_clean();
    return SaveResult::success(result.value());
  }
  return SaveResult::error("Failed to save patch");
}

SaveResult PatchSession::export_current_patch_as(ExportFormat format) {
  const auto &default_dir = directories_.paths().export_root;
  const std::string sanitized_name = sanitize_filename(
      current_patch_.name.empty() ? "patch" : current_patch_.name);

  switch (format) {
  case ExportFormat::DMP: {
    std::filesystem::path selected_path;
    auto result = platform::file_dialog::save_file(
        default_dir, sanitized_name + ".dmp",
        {{"DMP Files", {"dmp"}}, {"All Files", {"*"}}}, selected_path);

    if (result == platform::file_dialog::DialogResult::Ok) {
      if (selected_path.extension().empty()) {
        selected_path.replace_extension(".dmp");
      }

      if (formats::dmp::write_patch(current_patch_, selected_path)) {
        return SaveResult::success(selected_path);
      }
      return SaveResult::error("Failed to export DMP file: " +
                               selected_path.string());
    }
    return SaveResult::cancelled();
  }

  case ExportFormat::MML: {
    std::filesystem::path selected_path;
    auto result = platform::file_dialog::save_file(
        default_dir, sanitized_name + ".mml",
        {{"MML Files", {"mml"}}, {"All Files", {"*"}}}, selected_path);

    if (result == platform::file_dialog::DialogResult::Ok) {
      if (selected_path.extension().empty()) {
        selected_path.replace_extension(".mml");
      }

      if (formats::ctrmml::write_patch(current_patch_, selected_path)) {
        return SaveResult::success(selected_path);
      }
      return SaveResult::error("Failed to export MML file: " +
                               selected_path.string());
    }
    return SaveResult::cancelled();
  }
  }

  return SaveResult::error("Unknown export format");
}

bool PatchSession::current_patch_is_user_patch() const {
  const bool has_supported_extension = current_patch_path_.ends_with(".gin") ||
                                       current_patch_path_.ends_with(".ginpkg");
  const bool is_user_directory = !current_patch_path_.empty() &&
                                 current_patch_path_.rfind("user/", 0) == 0 &&
                                 has_supported_extension;
  return is_user_directory && original_patch_.name == current_patch_.name;
}

} // namespace patches

#endif
