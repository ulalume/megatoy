#include "star_rating.hpp"

#include <IconsFontAwesome7.h>
#include <algorithm>
#include <imgui.h>

namespace ui {

const std::array<std::string_view, 6> kStarLabels = {
    "-",
    ICON_FA_STAR,
    ICON_FA_STAR ICON_FA_STAR,
    ICON_FA_STAR ICON_FA_STAR ICON_FA_STAR,
    ICON_FA_STAR ICON_FA_STAR ICON_FA_STAR ICON_FA_STAR,
    ICON_FA_STAR ICON_FA_STAR ICON_FA_STAR ICON_FA_STAR ICON_FA_STAR,
};

float star_rating_width() {
  return ImGui::CalcTextSize(kStarLabels[5].data()).x +
         ImGui::GetStyle().FramePadding.x * 2.0f +
         ImGui::GetStyle().GrabMinSize;
}

bool star_rating_slider(const char *id, int &rating, const char *none_label) {
  rating = std::clamp(rating, 0, 5);
  ImGui::SetNextItemWidth(star_rating_width());
  return ImGui::SliderInt(id, &rating, 0, 5,
                          rating == 0 ? none_label
                                      : kStarLabels[rating].data(),
                          ImGuiSliderFlags_AlwaysClamp);
}

} // namespace ui
