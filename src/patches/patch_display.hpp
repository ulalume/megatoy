#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace patches {

inline constexpr std::string_view kBuiltinPresetRoot{"presets"};
inline constexpr std::string_view kBuiltinPresetDisplayName{"Built-in Presets"};

inline std::string display_preset_path(std::string relative_path) {
  if (!relative_path.starts_with(kBuiltinPresetRoot)) {
    return relative_path;
  }

  if (relative_path.size() == kBuiltinPresetRoot.size()) {
    return std::string(kBuiltinPresetDisplayName);
  }

  if (relative_path.size() > kBuiltinPresetRoot.size() &&
      relative_path[kBuiltinPresetRoot.size()] == '/') {
    return std::string(kBuiltinPresetDisplayName) +
           relative_path.substr(kBuiltinPresetRoot.size());
  }

  return relative_path;
}

inline std::string display_preset_path(const std::filesystem::path &path) {
  return display_preset_path(path.generic_string());
}

} // namespace patches
