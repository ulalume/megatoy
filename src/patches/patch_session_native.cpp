#if !defined(MEGATOY_PLATFORM_WEB)

#include "patch_session.hpp"

#include "formats/ctrmml.hpp"
#include "formats/dmp.hpp"
#include "patches/filename_utils.hpp"
#include "platform/file_dialog.hpp"

namespace patches {

SaveResult PatchSession::save_current_patch(bool force_overwrite) {
  auto result =
      repository_->save_patch(current_patch_, current_patch_.name, force_overwrite);
  switch (result.status) {
  case SavePatchResult::Status::Success:
    mark_as_clean();
    return SaveResult::success(result.path);
  case SavePatchResult::Status::Duplicate:
    return SaveResult::duplicated();
  case SavePatchResult::Status::Error:
    return SaveResult::error(result.error_message.empty()
                                 ? "Failed to save patch"
                                 : result.error_message);
  case SavePatchResult::Status::Unsupported:
  default:
    return SaveResult::error("Saving patches is unsupported on this platform");
  }
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

} // namespace patches

#endif
