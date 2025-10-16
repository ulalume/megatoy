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

void apply_theme(ThemeId id);
ThemeId current_theme();

ThemeId theme_id_from_storage_key(const std::string &key, ThemeId fallback);
const char *storage_key(ThemeId id);
const char *asset_subdirectory(ThemeId id);
std::string
themed_asset_relative_path(const std::string &filename,
                           const std::string &asset_category = "images");

} // namespace ui::styles
