#include "star_rating.hpp"

#include "gui/components/centred_glyph.hpp"
#include "gui/fonts/megatoy_icons.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>

namespace ui {
namespace {

/// The room between two stars, before the interface's scale.
constexpr float kStarGap = 2.0f;

} // namespace

const std::array<std::string_view, 6> kStarLabels = {
    "-",
    ICON_MEGATOY_STAR,
    ICON_MEGATOY_STAR ICON_MEGATOY_STAR,
    ICON_MEGATOY_STAR ICON_MEGATOY_STAR ICON_MEGATOY_STAR,
    ICON_MEGATOY_STAR ICON_MEGATOY_STAR ICON_MEGATOY_STAR ICON_MEGATOY_STAR,
    ICON_MEGATOY_STAR ICON_MEGATOY_STAR ICON_MEGATOY_STAR ICON_MEGATOY_STAR
        ICON_MEGATOY_STAR,
};

float star_rating_width() {
  return ImGui::CalcTextSize(ICON_MEGATOY_STAR).x * 5.0f +
         scale::px(kStarGap) * 4.0f + ImGui::GetStyle().FramePadding.x * 2.0f;
}

bool star_rating_slider(const char *id, int &rating, const char *name) {
  const ImGuiStyle &style = ImGui::GetStyle();
  const float star = ImGui::CalcTextSize(ICON_MEGATOY_STAR).x;
  const float step = star + scale::px(kStarGap);
  const ImVec2 min = ImGui::GetCursorScreenPos();
  const ImVec2 max(min.x + star_rating_width(),
                   min.y + ImGui::GetFrameHeight());
  ImGui::InvisibleButton(id, ImVec2(max.x - min.x, max.y - min.y));
  ImGui::SetItemTooltip("%s", name);

  const int before = std::clamp(rating, 0, 5);
  rating = before;
  if (ImGui::IsItemActive()) {
    const float x = ImGui::GetIO().MousePos.x - (min.x + style.FramePadding.x);
    rating = x < 0.0f ? 0 : std::clamp(static_cast<int>(x / step) + 1, 1, 5);
  }

  ImGui::RenderFrame(
      min, max,
      ImGui::GetColorU32(ImGui::IsItemActive()    ? ImGuiCol_FrameBgActive
                         : ImGui::IsItemHovered() ? ImGuiCol_FrameBgHovered
                                                  : ImGuiCol_FrameBg),
      true, style.FrameRounding);
  const ImU32 filled = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 faint =
      ImGui::GetColorU32(styles::color(styles::MegatoyCol::TextMuted));
  for (int i = 0; i < 5; ++i) {
    const float left = min.x + style.FramePadding.x + step * i;
    draw_centred_glyph(i < rating ? ICON_MEGATOY_STAR
                                  : ICON_MEGATOY_STAR_OUTLINE,
                       ImVec2(left, min.y), ImVec2(left + star, max.y),
                       i < rating ? filled : faint);
  }
  return rating != before;
}

} // namespace ui
