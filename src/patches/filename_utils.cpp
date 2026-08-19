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

} // namespace patches
