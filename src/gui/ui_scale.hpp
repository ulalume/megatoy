#pragma once

#include <imgui.h>

namespace ui::scale {

/// Stored preference meaning "follow the display".
inline constexpr float kAuto = 0.0f;
inline constexpr float kMin = 1.0f;
inline constexpr float kMax = 3.0f;

/// Factors the Preferences combo offers, in the order it lists them.
inline constexpr float kChoices[] = {1.0f, 1.25f, 1.5f, 1.75f,
                                     2.0f, 2.5f,  3.0f};

/**
 * Turn a stored preference into the factor to draw at.
 *
 * `display_scale` is what the platform asks content to be enlarged by;
 * anything at or below zero means it could not be determined.
 */
float resolve(float preference, float display_scale);

/// The factor the UI is drawn at.
inline float current() { return ImGui::GetStyle().FontScaleMain; }

/// Scale a measurement written in unscaled pixels.
inline float px(float value) { return value * current(); }
inline ImVec2 px(const ImVec2 &value) {
  return ImVec2(px(value.x), px(value.y));
}

} // namespace ui::scale
