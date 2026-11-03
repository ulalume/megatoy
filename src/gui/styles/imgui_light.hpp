#include "megatoy_style.hpp"
#include <imgui.h>

namespace ui::styles::imgui_light {
inline void apply() {
  ImGui::StyleColorsLight();
  ImVec4 *colors = ImGui::GetStyle().Colors;
  auto &palette = ::ui::styles::mutable_style();
  palette.colors[static_cast<int>(MegatoyCol::StatusSuccess)] =
      ImVec4(0.16f, 0.55f, 0.20f, 1.0f);
  palette.colors[static_cast<int>(MegatoyCol::StatusError)] =
      ImVec4(0.75f, 0.20f, 0.20f, 1.0f);
  palette.colors[static_cast<int>(MegatoyCol::StatusWarning)] =
      ImVec4(0.95f, 0.55f, 0.15f, 1.0f);
  palette.colors[static_cast<int>(MegatoyCol::TextMuted)] =
      ImVec4(0.35f, 0.35f, 0.40f, 1.0f);
  palette.colors[static_cast<int>(MegatoyCol::TextHighlight)] =
      ImVec4(0.20f, 0.40f, 0.70f, 1.0f);
  palette.colors[static_cast<int>(MegatoyCol::TextOnWhiteKey)] =
      ImVec4(0.10f, 0.12f, 0.20f, 1.0f);
  palette.colors[static_cast<int>(MegatoyCol::TextOnBlackKey)] =
      ImVec4(0.95f, 0.97f, 1.00f, 1.0f);
  palette.colors[static_cast<int>(MegatoyCol::PianoKeyBorder)] =
      colors[ImGuiCol_Border];
  palette.colors[static_cast<int>(MegatoyCol::PianoWhiteKey)] =
      ImVec4(0.97f, 0.98f, 0.99f, 1.0f);
  palette.colors[static_cast<int>(MegatoyCol::PianoWhiteKeyPressed)] =
      colors[ImGuiCol_FrameBgActive];
  palette.colors[static_cast<int>(MegatoyCol::PianoBlackKey)] =
      ImVec4(0.10f, 0.12f, 0.20f, 1.0f);
  palette.colors[static_cast<int>(MegatoyCol::PianoBlackKeyPressed)] =
      colors[ImGuiCol_FrameBgActive];
  palette.colors[static_cast<int>(MegatoyCol::ModalBg)] =
      colors[ImGuiCol_PopupBg];
  ::ui::styles::derive_widget_colors();
}
} // namespace ui::styles::imgui_light
