#include "theme.hpp"

#include <algorithm>
#include <stdexcept>

namespace ui::styles {
namespace {

const std::vector<ThemeDefinition> &all_theme_definitions() {
  static const std::vector<ThemeDefinition> themes = {
      {ThemeId::MegatoyDark, "Megatoy Dark", "megatoy-dark", "megatoy_dark"},
      {ThemeId::MegatoyLight, "Megatoy Light", "megatoy-light",
       "megatoy_light"},
      {ThemeId::ImGuiDark, "ImGui Dark", "imgui-dark", "imgui_dark"},
  };
  return themes;
}

} // namespace

const ThemeDefinition &theme_definition(ThemeId id) {
  const auto &themes = all_theme_definitions();
  auto it =
      std::find_if(themes.begin(), themes.end(),
                   [id](const ThemeDefinition &def) { return def.id == id; });
  if (it == themes.end()) {
    throw std::runtime_error("Unknown theme id");
  }
  return *it;
}

const std::vector<ThemeDefinition> &available_themes() {
  return all_theme_definitions();
}

ThemeId theme_id_from_storage_key(const std::string &key, ThemeId fallback) {
  const auto &themes = all_theme_definitions();
  auto it = std::find_if(
      themes.begin(), themes.end(),
      [&key](const ThemeDefinition &def) { return key == def.storage_key; });
  return it != themes.end() ? it->id : fallback;
}

const char *storage_key(ThemeId id) { return theme_definition(id).storage_key; }

const char *asset_subdirectory(ThemeId id) {
  return theme_definition(id).asset_subdirectory;
}

} // namespace ui::styles
