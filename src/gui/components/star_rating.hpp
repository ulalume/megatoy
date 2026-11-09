#pragma once

#include <array>
#include <imgui.h>
#include <string_view>

namespace ui {

/// "-", then 1..5 stars, as icon strings.
extern const std::array<std::string_view, 6> kStarLabels;

/// Five stars side by side.
float stars_width();

/// Five stars from `min`, centred in `height`: `filled` of them filled and
/// the rest outlined and faint.
void draw_stars(ImVec2 min, float height, int filled);

/// star_rating_slider's width, with `padding` at either end of the stars:
/// the frame's, by default.
float star_rating_width(float padding = -1.0f);

/**
 * Five stars, those up to `rating` filled and the rest outlined and faint;
 * drag or click to pick 0..5, left of the first star for 0. `name` is its
 * tooltip. Returns true when `rating` changed this frame, like any slider.
 */
bool star_rating_slider(const char *id, int &rating, const char *name,
                        float padding = -1.0f);

} // namespace ui
