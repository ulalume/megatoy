#include "star_rating.hpp"

#include "gui/components/centred_glyph.hpp"
#include "gui/fonts/megatoy_icons.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include <algorithm>
#include <imgui.h>

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

float stars_width() {
  return ImGui::CalcTextSize(ICON_MEGATOY_STAR).x * 5.0f +
         scale::px(kStarGap) * 4.0f;
}

void draw_stars(ImVec2 min, float height, int filled) {
  const float star = ImGui::CalcTextSize(ICON_MEGATOY_STAR).x;
  const float step = star + scale::px(kStarGap);
  const ImU32 lit = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 faint =
      ImGui::GetColorU32(styles::color(styles::MegatoyCol::TextMuted));
  for (int i = 0; i < 5; ++i) {
    const float left = min.x + step * i;
    draw_centred_glyph(i < filled ? ICON_MEGATOY_STAR
                                  : ICON_MEGATOY_STAR_OUTLINE,
                       ImVec2(left, min.y), ImVec2(left + star, min.y + height),
                       i < filled ? lit : faint);
  }
}

namespace {

float padding_or_frame(float padding) {
  return padding < 0.0f ? ImGui::GetStyle().FramePadding.x : padding;
}

} // namespace

float star_rating_width(float padding) {
  return stars_width() + padding_or_frame(padding) * 2.0f;
}

bool star_rating_slider(const char *id, int &rating, const char *name,
                        float padding) {
  const float left = ImGui::GetCursorScreenPos().x + padding_or_frame(padding);
  const float step =
      ImGui::CalcTextSize(ICON_MEGATOY_STAR).x + scale::px(kStarGap);
  ImGui::InvisibleButton(
      id, ImVec2(star_rating_width(padding), ImGui::GetFrameHeight()));
  ImGui::SetItemTooltip("%s", name);

  const int before = std::clamp(rating, 0, 5);
  rating = before;
  if (ImGui::IsItemActive()) {
    const float x = ImGui::GetIO().MousePos.x - left;
    rating = x < 0.0f ? 0 : std::clamp(static_cast<int>(x / step) + 1, 1, 5);
  }
  draw_stars(ImVec2(left, ImGui::GetItemRectMin().y), ImGui::GetFrameHeight(),
             rating);
  return rating != before;
}

} // namespace ui
