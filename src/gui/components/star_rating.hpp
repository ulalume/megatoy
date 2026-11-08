#pragma once

#include <array>
#include <string_view>

namespace ui {

/// "-", then 1..5 stars, as icon strings.
extern const std::array<std::string_view, 6> kStarLabels;

/// Wide enough for five stars.
float star_rating_width();

/**
 * Five stars, those up to `rating` filled and the rest outlined and faint;
 * drag or click to pick 0..5, left of the first star for 0. `name` is its
 * tooltip. Returns true when `rating` changed this frame, like any slider.
 */
bool star_rating_slider(const char *id, int &rating, const char *name);

} // namespace ui
