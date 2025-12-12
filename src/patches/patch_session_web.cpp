#if defined(MEGATOY_PLATFORM_WEB)

#include "patch_session.hpp"

#include "formats/ctrmml.hpp"
#include "formats/dmp.hpp"
#include "patches/filename_utils.hpp"
#include "platform/web/web_download.hpp"
#include "platform/web/web_patch_store.hpp"

namespace patches {

SaveResult PatchSession::save_current_patch(bool force_overwrite) {
  const std::string sanitized_name = sanitize_filename(
      current_patch_.name.empty() ? "patch" : current_patch_.name);
  if (!force_overwrite && platform::web::patch_store::exists(sanitized_name)) {
    return SaveResult::duplicated();
  }
  if (platform::web::patch_store::save(current_patch_, sanitized_name)) {
    mark_as_clean();
    repository_->refresh();
    return SaveResult::success(
        std::filesystem::path("localStorage/" + sanitized_name));
  }
  return SaveResult::error("Failed to save patch in browser storage");
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

bool PatchSession::current_patch_is_user_patch() const {
  const bool is_local_storage =
      !current_patch_path_.empty() &&
      current_patch_path_.rfind("localStorage/", 0) == 0;
  const bool has_supported_extension = current_patch_path_.ends_with(".gin") ||
                                       current_patch_path_.ends_with(".ginpkg");
  const bool is_user_directory = !current_patch_path_.empty() &&
                                 current_patch_path_.rfind("user/", 0) == 0 &&
                                 has_supported_extension;
  return (is_user_directory || is_local_storage) &&
         original_patch_.name == current_patch_.name;
}

} // namespace patches

#endif
