#include "megatoy_style.hpp"
#include <imgui.h>

namespace ui::styles::imgui_dark {
inline void apply() {
  ImGui::StyleColorsDark();
  ImVec4 *colors = ImGui::GetStyle().Colors;
  auto &palette = ::ui::styles::mutable_style();
  palette.colors[static_cast<int>(MegatoyCol::StatusSuccess)] =
      ImVec4(0.34f, 0.76f, 0.38f, 1.0f);
  palette.colors[static_cast<int>(MegatoyCol::StatusError)] =
      ImVec4(0.92f, 0.32f, 0.32f, 1.0f);
  palette.colors[static_cast<int>(MegatoyCol::StatusWarning)] =
      ImVec4(0.98f, 0.65f, 0.22f, 1.0f);
  palette.colors[static_cast<int>(MegatoyCol::TextMuted)] =
      ImVec4(0.60f, 0.60f, 0.60f, 1.0f);
  palette.colors[static_cast<int>(MegatoyCol::TextHighlight)] =
      ImVec4(0.98f, 0.85f, 0.30f, 1.0f);
  palette.colors[static_cast<int>(MegatoyCol::TextOnWhiteKey)] =
      ImVec4(0.18f, 0.20f, 0.23f, 1.0f);
  palette.colors[static_cast<int>(MegatoyCol::TextOnBlackKey)] =
      ImVec4(0.96f, 0.96f, 0.96f, 1.0f);
  palette.colors[static_cast<int>(MegatoyCol::PianoKeyBorder)] =
      ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
  palette.colors[static_cast<int>(MegatoyCol::PianoWhiteKey)] =
      ImVec4(0.86f, 0.88f, 0.92f, 1.0f);
  palette.colors[static_cast<int>(MegatoyCol::PianoWhiteKeyPressed)] =
      ImVec4(0.95f, 0.96f, 0.99f, 1.0f);
  palette.colors[static_cast<int>(MegatoyCol::PianoBlackKey)] =
      colors[ImGuiCol_FrameBg];
  palette.colors[static_cast<int>(MegatoyCol::PianoBlackKeyPressed)] =
      colors[ImGuiCol_FrameBgActive];
}
} // namespace ui::styles::imgui_dark
