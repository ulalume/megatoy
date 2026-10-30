#pragma once

#include <string>
#include <vector>

namespace ui::styles {

enum class ThemeId {
  MegatoyDark,
  MegatoyLight,
  ImGuiDark,
};

struct ThemeDefinition {
  ThemeId id;
  const char *display_name;
  const char *storage_key;
  const char *asset_subdirectory;
};

const ThemeDefinition &theme_definition(ThemeId id);
const std::vector<ThemeDefinition> &available_themes();

ThemeId theme_id_from_storage_key(const std::string &key, ThemeId fallback);
const char *storage_key(ThemeId id);
const char *asset_subdirectory(ThemeId id);

} // namespace ui::styles
