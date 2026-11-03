#include "megatoy_style.hpp"
#include <imgui.h>

namespace ui::styles::megatoy_dark {

/// The colors the theme is built from. Each widget color below is one of
/// these, often at a lower alpha.
struct Palette {
  /// Hovered, active and selected things.
  ImVec4 accent{0.92f, 0.18f, 0.29f, 1.00f};
  ImVec4 text{0.86f, 0.93f, 0.89f, 1.00f};
  /// Window backgrounds.
  ImVec4 base{0.13f, 0.14f, 0.17f, 1.00f};
  /// Darker than base: borders, title bars, scrollbar tracks.
  ImVec4 shade{0.11f, 0.11f, 0.13f, 1.00f};
  /// Lighter than base: input boxes, tabs, popups.
  ImVec4 surface{0.20f, 0.22f, 0.27f, 1.00f};
  /// Separators and strong table borders.
  ImVec4 line{0.31f, 0.31f, 0.35f, 1.00f};
  /// Tint of buttons at rest.
  ImVec4 button{0.47f, 0.77f, 0.83f, 1.00f};
  ImVec4 white_key{0.88f, 0.92f, 0.90f, 1.00f};
  ImVec4 black_key{0.22f, 0.25f, 0.28f, 1.00f};
  ImVec4 muted{0.70f, 0.70f, 0.70f, 1.00f};
  ImVec4 highlight{1.00f, 1.00f, 0.45f, 1.00f};
  ImVec4 success{0.30f, 0.85f, 0.40f, 1.00f};
  ImVec4 error{0.95f, 0.35f, 0.40f, 1.00f};
  ImVec4 warning{1.00f, 0.70f, 0.25f, 1.00f};
};

inline ImVec4 with_alpha(ImVec4 color, float alpha) {
  color.w = alpha;
  return color;
}

inline void apply(const Palette &p = Palette{}) {
  ImGui::StyleColorsDark();

  const ImVec4 clear(0.00f, 0.00f, 0.00f, 0.00f);
  ImVec4 *colors = ImGui::GetStyle().Colors;
  colors[ImGuiCol_Text] = with_alpha(p.text, 0.78f);
  colors[ImGuiCol_TextDisabled] = with_alpha(p.text, 0.28f);
  colors[ImGuiCol_WindowBg] = p.base;
  colors[ImGuiCol_ChildBg] = clear;
  // Distinct from FrameBg, so a text input or slider inside a popup still
  // reads as a box.
  colors[ImGuiCol_PopupBg] = with_alpha(p.surface, 0.98f);
  colors[ImGuiCol_Border] = p.shade;
  colors[ImGuiCol_BorderShadow] = clear;
  colors[ImGuiCol_FrameBg] = p.surface;
  colors[ImGuiCol_FrameBgHovered] = with_alpha(p.accent, 0.78f);
  colors[ImGuiCol_FrameBgActive] = p.accent;
  colors[ImGuiCol_TitleBg] = p.shade;
  colors[ImGuiCol_TitleBgActive] = p.shade;
  colors[ImGuiCol_TitleBgCollapsed] = with_alpha(p.surface, 0.75f);
  colors[ImGuiCol_MenuBarBg] = with_alpha(p.surface, 0.47f);
  colors[ImGuiCol_ScrollbarBg] = p.shade;
  colors[ImGuiCol_ScrollbarGrab] = p.surface;
  colors[ImGuiCol_ScrollbarGrabHovered] = with_alpha(p.accent, 0.78f);
  colors[ImGuiCol_ScrollbarGrabActive] = p.accent;
  colors[ImGuiCol_CheckMark] = with_alpha(p.text, 0.78f);
  colors[ImGuiCol_SliderGrab] = with_alpha(p.text, 0.20f);
  colors[ImGuiCol_SliderGrabActive] = with_alpha(p.text, 0.39f);
  colors[ImGuiCol_Button] = with_alpha(p.button, 0.14f);
  colors[ImGuiCol_ButtonHovered] = with_alpha(p.accent, 0.86f);
  colors[ImGuiCol_ButtonActive] = p.accent;
  colors[ImGuiCol_Header] = with_alpha(p.accent, 0.41f);
  colors[ImGuiCol_HeaderHovered] = with_alpha(p.accent, 0.86f);
  colors[ImGuiCol_HeaderActive] = p.accent;
  colors[ImGuiCol_Separator] = with_alpha(p.line, 0.78f);
  colors[ImGuiCol_SeparatorHovered] = with_alpha(p.accent, 0.78f);
  colors[ImGuiCol_SeparatorActive] = p.accent;
  colors[ImGuiCol_ResizeGrip] = p.surface;
  colors[ImGuiCol_ResizeGripHovered] = with_alpha(p.accent, 0.78f);
  colors[ImGuiCol_ResizeGripActive] = p.accent;
  colors[ImGuiCol_InputTextCursor] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
  colors[ImGuiCol_TabHovered] = with_alpha(p.accent, 0.78f);
  colors[ImGuiCol_Tab] = p.surface;
  colors[ImGuiCol_TabSelected] = with_alpha(p.accent, 0.41f);
  colors[ImGuiCol_TabSelectedOverline] = with_alpha(p.accent, 0.41f);
  colors[ImGuiCol_TabDimmed] = p.surface;
  colors[ImGuiCol_TabDimmedSelected] = p.line;
  colors[ImGuiCol_TabDimmedSelectedOverline] = clear;
  colors[ImGuiCol_DockingPreview] = p.shade;
  colors[ImGuiCol_DockingEmptyBg] = p.base;
  colors[ImGuiCol_PlotLines] = with_alpha(p.text, 0.63f);
  colors[ImGuiCol_PlotLinesHovered] = p.accent;
  colors[ImGuiCol_PlotHistogram] = with_alpha(p.text, 0.63f);
  colors[ImGuiCol_PlotHistogramHovered] = p.accent;
  colors[ImGuiCol_TableHeaderBg] = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
  colors[ImGuiCol_TableBorderStrong] = p.line;
  colors[ImGuiCol_TableBorderLight] = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
  colors[ImGuiCol_TableRowBg] = clear;
  colors[ImGuiCol_TableRowBgAlt] = with_alpha(p.shade, 0.80f);
  colors[ImGuiCol_TextLink] = p.text;
  colors[ImGuiCol_TextSelectedBg] = with_alpha(p.accent, 0.43f);
  colors[ImGuiCol_TreeLines] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
  colors[ImGuiCol_DragDropTarget] = p.accent;
  colors[ImGuiCol_NavCursor] = p.accent;
  colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
  colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
  colors[ImGuiCol_ModalWindowDimBg] = with_alpha(p.surface, 0.73f);

  ImVec4 *megatoy = ::ui::styles::mutable_style().colors;
  const auto at = [](MegatoyCol col) { return static_cast<int>(col); };
  megatoy[at(MegatoyCol::StatusSuccess)] = p.success;
  megatoy[at(MegatoyCol::StatusError)] = p.error;
  megatoy[at(MegatoyCol::StatusWarning)] = p.warning;
  megatoy[at(MegatoyCol::TextMuted)] = p.muted;
  megatoy[at(MegatoyCol::TextHighlight)] = p.highlight;
  megatoy[at(MegatoyCol::TextOnWhiteKey)] = p.base;
  megatoy[at(MegatoyCol::TextOnBlackKey)] = p.text;
  megatoy[at(MegatoyCol::PianoKeyBorder)] = p.shade;
  megatoy[at(MegatoyCol::PianoWhiteKey)] = p.white_key;
  megatoy[at(MegatoyCol::PianoWhiteKeyPressed)] = p.accent;
  megatoy[at(MegatoyCol::PianoBlackKey)] = p.black_key;
  megatoy[at(MegatoyCol::PianoBlackKeyPressed)] = p.accent;
  megatoy[at(MegatoyCol::ModalBg)] = with_alpha(p.base, 0.98f);
  ::ui::styles::derive_widget_colors();
}

} // namespace ui::styles::megatoy_dark
