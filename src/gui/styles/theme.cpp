#include "theme.hpp"
#include "imgui_dark.hpp"
#include "imgui_light.hpp"
#include "megatoy_dark.hpp"

#include <imgui.h>

namespace ui::styles {
ThemeId g_current_theme = ThemeId::Megatoy;

void apply_theme(ThemeId id) {
  g_current_theme = id;
  if (ImGui::GetCurrentContext() == nullptr) {
    return;
  }

  switch (id) {
  case ThemeId::Megatoy:
    ui::styles::megatoy_dark::apply();
    break;
  case ThemeId::ImGuiDark:
    ui::styles::imgui_dark::apply();
    break;
  case ThemeId::ImGuiLight:
    ui::styles::imgui_light::apply();
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
  style.FramePadding = ImVec2(4, 6);
  style.DockingSeparatorSize = 4;
  style.TabBarBorderSize = 2;

  apply_theme(id);

  style.ScaleAllSizes(scale);
  style.FontScaleMain = scale;
}

ThemeId current_theme() { return g_current_theme; }

} // namespace ui::styles
