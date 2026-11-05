#include "megatoy_style.hpp"

#include <imgui.h>

namespace ui::styles {
namespace {

MegatoyStyle g_style{};

} // namespace

MegatoyStyle &mutable_style() { return g_style; }

const MegatoyStyle &style() { return g_style; }

void derive_widget_colors() {
  const ImVec4 *imgui = ImGui::GetStyle().Colors;
  ImVec4 *colors = g_style.colors;
  const auto set = [colors](MegatoyCol col, const ImVec4 &value) {
    colors[static_cast<int>(col)] = value;
  };
  const auto faded = [](ImVec4 value, float alpha) {
    value.w *= alpha;
    return value;
  };
  const ImVec4 clear(0.0f, 0.0f, 0.0f, 0.0f);
  const ImVec4 highlight = color(MegatoyCol::TextHighlight);
  set(MegatoyCol::OperatorBg, clear);
  set(MegatoyCol::OperatorBgHovered, clear);
  set(MegatoyCol::OperatorBgSelected, clear);
  set(MegatoyCol::OperatorBorder, imgui[ImGuiCol_Border]);
  set(MegatoyCol::OperatorBorderHovered, imgui[ImGuiCol_SeparatorHovered]);
  set(MegatoyCol::OperatorBorderSelected, faded(highlight, 0.5f));
  set(MegatoyCol::OperatorBorderPrimary, highlight);
  set(MegatoyCol::CarrierBadge, imgui[ImGuiCol_FrameBgActive]);
  set(MegatoyCol::EnvelopeBg, clear);
  set(MegatoyCol::EnvelopeCurve, imgui[ImGuiCol_Text]);
  set(MegatoyCol::EnvelopeCurveActive, imgui[ImGuiCol_FrameBgActive]);
  set(MegatoyCol::EnvelopeRelease, faded(imgui[ImGuiCol_Text], 0.3f));
  set(MegatoyCol::EnvelopeReleaseActive,
      faded(imgui[ImGuiCol_FrameBgActive], 0.3f));
  // A handle is a hint until it is wanted: dimmer than the line it sits on
  // until the pointer is on it.
  set(MegatoyCol::EnvelopeHandle, faded(imgui[ImGuiCol_Text], 0.55f));
  set(MegatoyCol::EnvelopeHandleActive, imgui[ImGuiCol_FrameBgActive]);
  set(MegatoyCol::EnvelopeGrid, imgui[ImGuiCol_Separator]);
  set(MegatoyCol::EnvelopeLabel, faded(imgui[ImGuiCol_Text], 0.6f));
  set(MegatoyCol::EnvelopeVoice, imgui[ImGuiCol_Text]);
  set(MegatoyCol::EnvelopeVoiceCursor, imgui[ImGuiCol_FrameBgActive]);
  set(MegatoyCol::WaveformBg, imgui[ImGuiCol_FrameBg]);
  set(MegatoyCol::WaveformBorder, imgui[ImGuiCol_Border]);
  set(MegatoyCol::WaveformGrid, faded(imgui[ImGuiCol_Border], 0.6f));
  set(MegatoyCol::WaveformLeft, imgui[ImGuiCol_PlotLines]);
  set(MegatoyCol::WaveformRight, faded(highlight, 0.75f));
  set(MegatoyCol::WaveformClipped, color(MegatoyCol::StatusWarning));
  set(MegatoyCol::SpectrumBar, faded(imgui[ImGuiCol_PlotHistogram], 0.85f));
  set(MegatoyCol::SpectrumGrid, faded(imgui[ImGuiCol_Border], 0.5f));
  set(MegatoyCol::SpectrumLabel, faded(color(MegatoyCol::TextMuted), 0.9f));
  set(MegatoyCol::PreviewBorder, imgui[ImGuiCol_Separator]);
  set(MegatoyCol::PatchEditorBg, imgui[ImGuiCol_WindowBg]);
  set(MegatoyCol::DialogPaneBg, imgui[ImGuiCol_ChildBg]);
  set(MegatoyCol::DialogPaneBorder, imgui[ImGuiCol_Border]);
  set(MegatoyCol::AboutBg, color(MegatoyCol::ModalBg));
  set(MegatoyCol::AboutIconBorder, imgui[ImGuiCol_WindowBg]);
  set(MegatoyCol::PanelBorder, imgui[ImGuiCol_Border]);
  set(MegatoyCol::SectionHeader, imgui[ImGuiCol_FrameBg]);
  set(MegatoyCol::SectionHeaderHovered, imgui[ImGuiCol_HeaderHovered]);
  set(MegatoyCol::SectionHeaderActive, imgui[ImGuiCol_HeaderActive]);
}

const ImVec4 &color(MegatoyCol col) {
  return g_style.colors[static_cast<int>(col)];
}

ImU32 color_u32(MegatoyCol col) {
  return ImGui::ColorConvertFloat4ToU32(color(col));
}

} // namespace ui::styles
