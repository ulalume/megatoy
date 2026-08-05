#include "platform/web/web_patch_export.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include "formats/ym2612_format_adapter.hpp"
#include "patches/filename_utils.hpp"
#include "platform/web/web_download.hpp"
#include "ym2612/patch.hpp"

namespace platform::web {

bool export_patch(const ym2612::Patch &patch, const std::string &name,
                  const std::string &extension_hint) {
  const std::string sanitized =
      patches::sanitize_filename(name.empty() ? "patch" : name);

  std::string extension = extension_hint.empty() ? ".dmp" : extension_hint;
  if (extension.front() != '.') {
    extension.insert(extension.begin(), '.');
  }

  const auto format = formats::adapter::format_for_extension(extension);
  if (!format) {
    return false;
  }

  for (const auto &info : formats::adapter::known_formats()) {
    if (info.format != *format || !info.can_write) {
      continue;
    }

    // Text formats go out as text so the browser saves them as such rather
    // than as an opaque blob.
    if (info.is_text) {
      auto text = formats::adapter::serialize_text(*format, patch);
      if (!text) {
        return false;
      }
      download_text(sanitized + extension, *text, "text/plain");
      return true;
    }

    auto data = formats::adapter::serialize(*format, patch);
    if (!data) {
      return false;
    }
    download_binary(sanitized + extension, *data,
                    "application/octet-stream");
    return true;
  }

  return false;
}

} // namespace platform::web

#endif
