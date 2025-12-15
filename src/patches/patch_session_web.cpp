#include "patch_session.hpp"

#include "formats/ctrmml.hpp"
#include "formats/dmp.hpp"
#include "patches/filename_utils.hpp"
#include "platform/web/web_download.hpp"

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
                                 ? "Failed to save patch in browser storage"
                                 : result.error_message);
  case SavePatchResult::Status::Unsupported:
  default:
    return SaveResult::error("Saving patches is unsupported on this platform");
  }
}

SaveResult PatchSession::export_current_patch_as(ExportFormat format) {
  const std::string sanitized_name = sanitize_filename(
      current_patch_.name.empty() ? "patch" : current_patch_.name);

  switch (format) {
  case ExportFormat::DMP: {
    const std::string filename = sanitized_name + ".dmp";
    auto data = formats::dmp::serialize_patch(current_patch_);
    platform::web::download_binary(filename, data, "application/octet-stream");
    return SaveResult::success(filename);
  }
  case ExportFormat::MML: {
    const std::string filename = sanitized_name + ".mml";
    auto text = formats::ctrmml::patch_to_string(current_patch_);
    platform::web::download_text(filename, text, "text/plain");
    return SaveResult::success(filename);
  }
  }
  return SaveResult::error("Unknown export format");
}

} // namespace patches
