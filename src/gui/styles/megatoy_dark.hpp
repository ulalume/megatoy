#include "megatoy_style.hpp"
#include <cstdint>
#include <imgui.h>

namespace ui::styles::megatoy_dark {

/// A color written as 0xRRGGBBAA.
constexpr ImVec4 hex(std::uint32_t rgba) {
  return ImVec4(static_cast<float>((rgba >> 24) & 0xFF) / 255.0f,
                static_cast<float>((rgba >> 16) & 0xFF) / 255.0f,
                static_cast<float>((rgba >> 8) & 0xFF) / 255.0f,
                static_cast<float>(rgba & 0xFF) / 255.0f);
}

/// `color` at `alpha` of its own opacity, so a translucent palette color
/// stays translucent wherever it is used.
constexpr ImVec4 faded(ImVec4 color, float alpha) {
  color.w *= alpha;
  return color;
}

/// The colors the theme is built from. Each widget color below is one of
/// these, often faded.
struct Palette {
  /// Hovered, active and selected things.
  ImVec4 accent = hex(0xEB2E4AFF);
  ImVec4 text = hex(0xF2FAFFFF);
  /// Window backgrounds.
  ImVec4 base = hex(0x101215FF);
  /// Darker than base: borders, title bars, scrollbar tracks.
  ImVec4 shade = hex(0x1C1E23FF);
  /// Lighter than base: input boxes, tabs.
  ImVec4 surface = faded(hex(0x95C5FFFF), 0.18f);
  /// Separators and strong table borders.
  ImVec4 line = faded(hex(0x95C5FFFF), 0.18f);
  /// Tint of buttons at rest.
  ImVec4 button = hex(0x95C5FFFF);
  ImVec4 white_key = faded(hex(0xF2FAFFFF), 0.8f);
  ImVec4 black_key = faded(hex(0x95C5FFFF), 0.15f);
  ImVec4 muted = faded(hex(0xCBE3FFFF), 0.6f);
  ImVec4 highlight = hex(0xEB2E4AFF);
  ImVec4 success = hex(0x4CD966FF);
  ImVec4 error = hex(0xF25966FF);
  ImVec4 warning = hex(0xFFB240FF);
};

inline void apply(const Palette &p = Palette{}) {
  ImGui::StyleColorsDark();

  const ImVec4 clear(0.00f, 0.00f, 0.00f, 0.00f);
  // Behind popups and dialogs, raised off the windows.
  const ImVec4 raised = hex(0x212834FF);
  // Quiet marks: slider grabs, graph captions and plots.
  const ImVec4 quiet = faded(hex(0xCBE3FFFF), 0.4f);
  ImGui::GetStyle().DisabledAlpha = 0.2f;
  ImVec4 *colors = ImGui::GetStyle().Colors;
  colors[ImGuiCol_Text] = faded(p.text, 0.85f);
  colors[ImGuiCol_TextDisabled] = faded(p.text, 0.28f);
  colors[ImGuiCol_WindowBg] = p.base;
  colors[ImGuiCol_ChildBg] = clear;
  colors[ImGuiCol_PopupBg] = raised;
  colors[ImGuiCol_Border] = p.shade;
  colors[ImGuiCol_BorderShadow] = clear;
  colors[ImGuiCol_FrameBg] = p.surface;
  colors[ImGuiCol_FrameBgHovered] = faded(p.accent, 0.78f);
  colors[ImGuiCol_FrameBgActive] = p.accent;
  colors[ImGuiCol_TitleBg] = p.shade;
  colors[ImGuiCol_TitleBgActive] = p.shade;
  colors[ImGuiCol_TitleBgCollapsed] = faded(p.surface, 0.75f);
  colors[ImGuiCol_MenuBarBg] = p.base;
  colors[ImGuiCol_ScrollbarBg] = p.shade;
  colors[ImGuiCol_ScrollbarGrab] = p.surface;
  colors[ImGuiCol_ScrollbarGrabHovered] = faded(p.accent, 0.78f);
  colors[ImGuiCol_ScrollbarGrabActive] = p.accent;
  colors[ImGuiCol_CheckMark] = faded(p.text, 0.78f);
  colors[ImGuiCol_CheckboxSelectedBg] = faded(p.accent, 0.25f);
  colors[ImGuiCol_SliderGrab] = quiet;
  colors[ImGuiCol_SliderGrabActive] = quiet;
  colors[ImGuiCol_Button] = faded(p.button, 0.18f);
  colors[ImGuiCol_ButtonHovered] = faded(p.accent, 0.86f);
  colors[ImGuiCol_ButtonActive] = p.accent;
  colors[ImGuiCol_Header] = faded(p.accent, 0.41f);
  colors[ImGuiCol_HeaderHovered] = faded(p.accent, 0.86f);
  colors[ImGuiCol_HeaderActive] = p.accent;
  colors[ImGuiCol_Separator] = faded(p.line, 0.78f);
  colors[ImGuiCol_SeparatorHovered] = faded(p.accent, 0.78f);
  colors[ImGuiCol_SeparatorActive] = p.accent;
  colors[ImGuiCol_ResizeGrip] = p.surface;
  colors[ImGuiCol_ResizeGripHovered] = faded(p.accent, 0.78f);
  colors[ImGuiCol_ResizeGripActive] = p.accent;
  colors[ImGuiCol_InputTextCursor] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
  colors[ImGuiCol_TabHovered] = faded(p.accent, 0.78f);
  colors[ImGuiCol_Tab] = clear;
  colors[ImGuiCol_TabSelected] = p.base;
  colors[ImGuiCol_TabDimmed] = clear;
  colors[ImGuiCol_TabDimmedSelected] = p.base;
  colors[ImGuiCol_DockingPreview] = p.shade;
  colors[ImGuiCol_DockingEmptyBg] = p.base;
  colors[ImGuiCol_PlotLines] = faded(p.text, 0.63f);
  colors[ImGuiCol_PlotLinesHovered] = p.accent;
  colors[ImGuiCol_PlotHistogram] = faded(p.text, 0.63f);
  colors[ImGuiCol_PlotHistogramHovered] = p.accent;
  colors[ImGuiCol_TableHeaderBg] = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
  colors[ImGuiCol_TableBorderStrong] = p.line;
  colors[ImGuiCol_TableBorderLight] = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
  colors[ImGuiCol_TableRowBg] = clear;
  colors[ImGuiCol_TableRowBgAlt] = faded(p.shade, 0.80f);
  colors[ImGuiCol_TextLink] = p.text;
  colors[ImGuiCol_TextSelectedBg] = faded(p.accent, 0.43f);
  colors[ImGuiCol_TreeLines] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
  colors[ImGuiCol_DragDropTarget] = p.accent;
  colors[ImGuiCol_DragDropTargetBg] = clear;
  colors[ImGuiCol_UnsavedMarker] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
  colors[ImGuiCol_NavCursor] = p.accent;
  colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
  colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
  colors[ImGuiCol_ModalWindowDimBg] = faded(p.base, 0.85f);

  ImVec4 *megatoy = ::ui::styles::mutable_style().colors;
  const auto at = [](MegatoyCol col) { return static_cast<int>(col); };
  megatoy[at(MegatoyCol::StatusSuccess)] = p.success;
  megatoy[at(MegatoyCol::StatusError)] = p.error;
  megatoy[at(MegatoyCol::StatusWarning)] = p.warning;
  megatoy[at(MegatoyCol::TextMuted)] = p.muted;
  megatoy[at(MegatoyCol::TextHighlight)] = p.highlight;
  megatoy[at(MegatoyCol::TextOnWhiteKey)] = p.base;
  megatoy[at(MegatoyCol::TextOnBlackKey)] = p.text;
  megatoy[at(MegatoyCol::PianoKeyBorder)] = p.base;
  megatoy[at(MegatoyCol::PianoWhiteKey)] = p.white_key;
  megatoy[at(MegatoyCol::PianoWhiteKeyPressed)] = p.accent;
  megatoy[at(MegatoyCol::PianoBlackKey)] = p.black_key;
  megatoy[at(MegatoyCol::PianoBlackKeyPressed)] = p.accent;
  megatoy[at(MegatoyCol::ModalBg)] = raised;
  ::ui::styles::derive_widget_colors();

  megatoy[at(MegatoyCol::OperatorBg)] = faded(p.shade, 0.5f);
  megatoy[at(MegatoyCol::OperatorBgHovered)] = hex(0xEB2E4A08);
  megatoy[at(MegatoyCol::OperatorBgSelected)] = hex(0xEB2E4A0A);
  megatoy[at(MegatoyCol::OperatorBorder)] = faded(p.line, 0.6f);
  megatoy[at(MegatoyCol::OperatorBorderHovered)] = faded(p.accent, 0.2f);
  megatoy[at(MegatoyCol::OperatorBorderSelected)] = faded(p.accent, 0.3f);
  megatoy[at(MegatoyCol::OperatorBorderPrimary)] = faded(p.accent, 0.8f);
  megatoy[at(MegatoyCol::CarrierBadge)] = faded(p.accent, 0.4f);
  megatoy[at(MegatoyCol::EnvelopeBg)] = hex(0x101215A0);
  megatoy[at(MegatoyCol::EnvelopeCurve)] = p.text;
  megatoy[at(MegatoyCol::EnvelopeRelease)] = hex(0x95C5FF15);
  megatoy[at(MegatoyCol::EnvelopeReleaseActive)] = faded(p.accent, 0.3f);
  megatoy[at(MegatoyCol::EnvelopeGrid)] = hex(0x95C5FF24);
  megatoy[at(MegatoyCol::EnvelopeLabel)] = quiet;
  megatoy[at(MegatoyCol::EnvelopeVoice)] = faded(p.accent, 0.85f);
  megatoy[at(MegatoyCol::EnvelopeVoiceCursor)] = p.accent;
  megatoy[at(MegatoyCol::EnvelopeBgCarrier)] = faded(p.accent, 0.01f);
  megatoy[at(MegatoyCol::EnvelopeFrameCarrier)] = faded(p.accent, 0.4f);
  megatoy[at(MegatoyCol::EnvelopeHandle)] = quiet;
  megatoy[at(MegatoyCol::WaveformLeft)] = quiet;
  megatoy[at(MegatoyCol::WaveformRight)] = quiet;
  megatoy[at(MegatoyCol::WaveformClipped)] = p.warning;
  megatoy[at(MegatoyCol::SpectrumBar)] = quiet;
  megatoy[at(MegatoyCol::DialogPaneBorder)] = p.line;
  megatoy[at(MegatoyCol::AboutBg)] = raised;
  megatoy[at(MegatoyCol::AboutIconBorder)] = clear;
  megatoy[at(MegatoyCol::PanelBorder)] = p.line;
  megatoy[at(MegatoyCol::SectionHeader)] = p.shade;
}

} // namespace ui::styles::megatoy_dark
