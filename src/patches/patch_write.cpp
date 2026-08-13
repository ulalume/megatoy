#include "patches/patch_write.hpp"

#include "formats/ginpkg.hpp"
#include "formats/patch_registry.hpp"
#include "formats/ym2612_format_adapter.hpp"

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
  if (!format) {
    return false;
  }
  if (formats::adapter::is_multi_patch(*format)) {
    return false;
  }
  for (const auto &info : formats::adapter::known_formats()) {
    if (info.format == *format) {
      return info.can_write;
    }
  }
  return false;
}

bool write_patch(const ym2612::Patch &patch,
                 const std::filesystem::path &path) {
  const auto extension = lowercase_extension(path);

  if (extension == ".ginpkg") {
    return formats::ginpkg::save_patch(path.parent_path(), patch,
                                       path.stem().string())
        .has_value();
  }

  auto &registry = formats::PatchRegistry::instance();
  for (const auto &format : registry.save_formats()) {
    if (format.extension == extension && format.is_text) {
      return registry.write_text(extension, patch, path);
    }
  }
  return registry.write(extension, patch, path);
}

} // namespace patches
