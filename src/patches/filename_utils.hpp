#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace patches {

// Check whether a character is valid in filenames
inline bool is_valid_filename_char(char c) {
  // Characters disallowed on Windows/Mac/Linux
  constexpr char invalid_chars[] = {'<',  '>', ':', '"', '/',
                                    '\\', '|', '?', '*'};
  // Control characters are also rejected
  if (c < 32 || c == 127) {
    return false;
  }
  // Check the invalid character list
  for (char invalid : invalid_chars) {
    if (c == invalid) {
      return false;
    }
  }
  return true;
}

// Normalise a filename by removing invalid characters
inline std::string sanitize_filename(const std::string &input) {
  std::string result;
  result.reserve(input.size());
  for (char c : input) {
    if (is_valid_filename_char(c)) {
      result += c;
    }
  }
  // Trim leading/trailing spaces and periods
  while (!result.empty() && (result.front() == ' ' || result.front() == '.')) {
    result.erase(0, 1);
  }
  while (!result.empty() && (result.back() == ' ' || result.back() == '.')) {
    result.pop_back();
  }
  return result;
}

std::filesystem::path append_extension_if_missing(std::filesystem::path path,
                                                  std::string_view extension);

/**
 * Why `stem` cannot name a new patch file with `extension` inside `folder`,
 * or an empty string when it can.
 *
 * The rules the rename prompt already applies -- a name is needed, and one
 * the filesystem will take -- plus the collision the chosen extension
 * decides: the same stem is free in one format and taken in another.
 */
std::string new_patch_name_error(const std::string &stem,
                                 std::string_view extension,
                                 const std::filesystem::path &folder);

/**
 * `base`, or the first of `base 2`, `base 3`, ... that is not among
 * `existing_names`. Names are compared ignoring case, as some filesystems do.
 */
std::string unused_folder_name(const std::vector<std::string> &existing_names,
                               std::string_view base);

/// Why `name` cannot name a new folder inside `parent`, or an empty string
/// when it can.
std::string new_folder_name_error(const std::string &name,
                                  const std::filesystem::path &parent);

} // namespace patches
