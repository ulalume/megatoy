#include "patches/patch_write.hpp"

#include "formats/ginpkg.hpp"
#include "formats/patch_registry.hpp"
#include "formats/ym2612_format_adapter.hpp"
#include "patches/filename_utils.hpp"

#include <algorithm>
#include <cctype>

namespace patches {

std::string lowercase_extension(const std::filesystem::path &path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return ext;
}

bool can_overwrite_in_place(const std::filesystem::path &path) {
  const auto extension = lowercase_extension(path);

  if (extension == ".ginpkg") {
    return true;
  }

  const auto format = formats::adapter::format_for_extension(extension);
  if (!format || formats::adapter::is_multi_patch(*format)) {
    return false;
  }
  // Only a format offered to save in; .gin, for one, is read only.
  const auto saves = formats::PatchRegistry::instance().save_formats();
  return std::any_of(saves.begin(), saves.end(), [&](const auto &info) {
    return info.extension == extension;
  });
}

bool write_patch(const ym2612::Patch &patch,
                 const std::filesystem::path &path) {
  const auto extension = lowercase_extension(path);

  if (extension == ".ginpkg") {
    return formats::ginpkg::save_patch_at(path, patch).has_value();
  }

  // Written beside `path` and moved over it once whole, so a write that
  // fails leaves the file there as it was. Hidden, so a scan of the folder
  // passes it by.
  const auto temporary =
      path.parent_path() / ("." + path.filename().string() + ".tmp");
  auto &registry = formats::PatchRegistry::instance();
  const auto formats = registry.save_formats();
  const bool text =
      std::any_of(formats.begin(), formats.end(), [&](const auto &format) {
        return format.extension == extension && format.is_text;
      });
  std::error_code error;
  if (text ? registry.write_text(extension, patch, temporary)
           : registry.write(extension, patch, temporary)) {
    std::filesystem::rename(temporary, path, error);
    if (!error) {
      return true;
    }
  }
  std::filesystem::remove(temporary, error);
  return false;
}

std::vector<platform::file_dialog::FileFilter>
save_dialog_filters(const std::vector<formats::SaveFormatInfo> &formats,
                    std::string_view first) {
  std::vector<platform::file_dialog::FileFilter> filters;
  const auto add = [&](const formats::SaveFormatInfo &format) {
    const std::string extension = format.extension.substr(1);
    const bool listed =
        std::any_of(filters.begin(), filters.end(), [&](const auto &filter) {
          return filter.extensions.front() == extension;
        });
    if (!listed) {
      filters.push_back({format.display_name(), {extension}});
    }
  };
  for (const auto &format : formats) {
    if (format.extension == first) {
      add(format);
    }
  }
  for (const auto &format : formats) {
    add(format);
  }
  return filters;
}

std::filesystem::path
with_save_extension(std::filesystem::path path,
                    const std::vector<formats::SaveFormatInfo> &formats,
                    std::string_view fallback) {
  const auto extension = lowercase_extension(path);
  const bool writable =
      std::any_of(formats.begin(), formats.end(), [&](const auto &format) {
        return format.extension == extension;
      });
  return writable ? path
                  : append_extension_if_missing(std::move(path), fallback);
}

} // namespace patches
