#include "patches/filename_utils.hpp"

#include <algorithm>
#include <cctype>

namespace patches {
namespace {

bool equals_case_insensitive(std::string_view lhs, std::string_view rhs) {
  return lhs.size() == rhs.size() &&
         std::equal(lhs.begin(), lhs.end(), rhs.begin(),
                    [](char left, char right) {
                      return std::tolower(static_cast<unsigned char>(left)) ==
                             std::tolower(static_cast<unsigned char>(right));
                    });
}

} // namespace

std::filesystem::path append_extension_if_missing(std::filesystem::path path,
                                                  std::string_view extension) {
  if (extension.empty()) {
    return path;
  }

  std::string normalized(extension);
  if (normalized.front() != '.') {
    normalized.insert(normalized.begin(), '.');
  }
  if (!equals_case_insensitive(path.extension().string(), normalized)) {
    path += normalized;
  }
  return path;
}

std::string new_patch_name_error(const std::string &stem,
                                 std::string_view extension,
                                 const std::filesystem::path &folder) {
  if (stem.empty()) {
    return "Name cannot be empty.";
  }
  // sanitize_filename also strips leading and trailing spaces and periods, so
  // a name it does not return unchanged is one the filesystem would not keep.
  if (sanitize_filename(stem) != stem) {
    return "Name contains invalid characters.";
  }

  const auto target = append_extension_if_missing(folder / stem, extension);
  std::error_code error;
  if (std::filesystem::exists(target, error)) {
    return "A patch with that name already exists.";
  }
  if (error) {
    return "Could not check the name.";
  }
  return {};
}

} // namespace patches
