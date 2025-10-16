#pragma once

#include <imgui.h>

namespace ui {

inline ImU32 color_with_alpha(ImU32 color, float alpha) {
  auto vec = ImGui::ColorConvertU32ToFloat4(color);
  return ImGui::ColorConvertFloat4ToU32(
      ImVec4(vec.x, vec.y, vec.z, vec.w * alpha));
}
inline ImVec4 color_with_alpha_vec4(ImVec4 color, float alpha) {
  return ImVec4(color.x, color.y, color.z, color.w * alpha);
}

} // namespace ui
