#include "theme.hpp"
#include "imgui_dark.hpp"
#include "megatoy_dark.hpp"
#include "megatoy_light.hpp"

#include <imgui.h>

namespace ui::styles {
ThemeId g_current_theme = ThemeId::MegatoyDark;

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

void apply_style(ThemeId id, float scale) {
  if (ImGui::GetCurrentContext() == nullptr) {
    apply_theme(id);
    return;
  }

  ImGuiStyle &style = ImGui::GetStyle();
  style = ImGuiStyle();
  style.ScrollbarSize = 8;
  style.ScrollbarRounding = 0;
  style.ScrollbarPadding = 0;
  style.SeparatorTextBorderSize = 1;
  style.DragDropTargetBorderSize = 1;
  style.FramePadding = ImVec2(4, 2);

  apply_theme(id);

  style.ScaleAllSizes(scale);
  style.FontScaleMain = scale;
}

ThemeId current_theme() { return g_current_theme; }

std::string themed_asset_relative_path(const std::string &filename,
                                       const std::string &asset_category) {
  std::string result;

  if (!asset_category.empty()) {
    result.append(asset_category);
    if (!result.empty() && result.back() != '/') {
      result.push_back('/');
    }
  }

  const char *subdir = asset_subdirectory(current_theme());
  if (subdir != nullptr && subdir[0] != '\0') {
    result.append(subdir);
    if (!result.empty() && result.back() != '/') {
      result.push_back('/');
    }
  }

  result.append(filename);
  return result;
}

} // namespace ui::styles
