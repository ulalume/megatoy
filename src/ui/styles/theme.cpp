#include "theme.hpp"
#include "imgui_dark.hpp"
#include "megatoy_dark.hpp"
#include "megatoy_light.hpp"

#include <algorithm>
#include <stdexcept>

#include <imgui.h>

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

ThemeId g_current_theme = ThemeId::MegatoyDark;

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

void apply_theme(ThemeId id) {
  g_current_theme = id;
  if (ImGui::GetCurrentContext() == nullptr) {
    return;
  }

  switch (id) {
  case ThemeId::MegatoyDark:
    ui::styles::megatoy_dark::apply();
    break;
  case ThemeId::MegatoyLight:
    ui::styles::megatoy_light::apply();
    break;
  case ThemeId::ImGuiDark:
    ui::styles::imgui_dark::apply();
    break;
  }
}

ThemeId current_theme() { return g_current_theme; }

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

std::string themed_asset_relative_path(const std::string &filename) {
  const char *subdir = asset_subdirectory(current_theme());
  if (subdir == nullptr || subdir[0] == '\0') {
    return filename;
  }
  std::string result(subdir);
  result.push_back('/');
  result.append(filename);
  return result;
}

} // namespace ui::styles
