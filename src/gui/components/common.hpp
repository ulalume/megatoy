#pragma once

#include <imgui.h>
#include <stdlib.h>
#include <filesystem>
#include <string>
#include <string_view>

namespace ui {

inline constexpr std::string_view kBuiltinPresetRoot{"presets"};
inline constexpr std::string_view kBuiltinPresetDisplayName{"Default Presets"};

inline void center_next_window() {
  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
}

inline void force_center_window() {
  ImVec2 pos = ImGui::GetWindowPos();
  ImVec2 size = ImGui::GetWindowSize();
  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImVec2 target_pos(center.x - size.x * 0.5f, center.y - size.y * 0.5f);

  if (abs(pos.x - target_pos.x) > 5.0f || abs(pos.y - target_pos.y) > 5.0f) {
    ImGui::SetWindowPos(target_pos, ImGuiCond_Always);
  }
}

inline ImU32 color_with_alpha(ImU32 color, float alpha) {
  auto vec = ImGui::ColorConvertU32ToFloat4(color);
  return ImGui::ColorConvertFloat4ToU32(
      ImVec4(vec.x, vec.y, vec.z, vec.w * alpha));
}
inline ImVec4 color_with_alpha_vec4(ImVec4 color, float alpha) {
  return ImVec4(color.x, color.y, color.z, color.w * alpha);
}

inline std::string display_preset_path(std::string relative_path) {
  if (!relative_path.starts_with(kBuiltinPresetRoot)) {
    return relative_path;
  }

  if (relative_path.size() == kBuiltinPresetRoot.size()) {
    return std::string(kBuiltinPresetDisplayName);
  }

  if (relative_path.size() > kBuiltinPresetRoot.size() &&
      relative_path[kBuiltinPresetRoot.size()] == '/') {
    return std::string(kBuiltinPresetDisplayName) +
           relative_path.substr(kBuiltinPresetRoot.size());
  }

  return relative_path;
}

inline std::string display_preset_path(const std::filesystem::path &path) {
  return display_preset_path(path.generic_string());
}

} // namespace ui
