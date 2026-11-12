#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace megatoy::utf8 {

/**
 * A path from UTF-8 and back, on every platform. Going through
 * std::u8string keeps it UTF-8; building a path straight from a narrow
 * string would decode it with the native narrow encoding (the ANSI codepage
 * on Windows) and mangle any non-ASCII path.
 */
inline std::filesystem::path to_path(std::string_view utf8) {
  return std::filesystem::path(std::u8string(
      reinterpret_cast<const char8_t *>(utf8.data()), utf8.size()));
}
inline std::string from_path(const std::filesystem::path &path) {
  const std::u8string utf8 = path.u8string();
  return std::string(reinterpret_cast<const char *>(utf8.data()), utf8.size());
}

std::string trim_incomplete_suffix(std::string_view input);
std::string truncate(std::string_view input, std::size_t max_bytes);

} // namespace megatoy::utf8
