#pragma once

#include <imgui.h>

namespace ui::styles {

enum class MegatoyCol : int {
  StatusSuccess,
  StatusError,
  StatusWarning,
  TextMuted,
  TextHighlight,
  TextOnWhiteKey,
  TextOnBlackKey,
  PianoKeyBorder,
  PianoWhiteKey,
  PianoWhiteKeyPressed,
  PianoBlackKey,
  PianoBlackKeyPressed,
  /// Behind a modal dialog. Menus, combos and tooltips take ImGuiCol_PopupBg.
  ModalBg,
  /// An operator's frame: its fill and its border, at rest, under the
  /// pointer, selected, and as the lead of the selection.
  OperatorBg,
  OperatorBgHovered,
  OperatorBgSelected,
  OperatorBorder,
  OperatorBorderHovered,
  OperatorBorderSelected,
  OperatorBorderPrimary,
  /// Behind the Carrier label of an operator that reaches the output.
  CarrierBadge,
  /// The envelope graph: its background, the curve, the stretch a hovered or
  /// dragged slider sets, the area under the release with and without RR's
  /// slider lit, the handles at rest and under the pointer or dragged, the
  /// grid and frame, the captions, the curves of sounding voices and their
  /// cursors.
  EnvelopeBg,
  EnvelopeCurve,
  EnvelopeCurveActive,
  EnvelopeRelease,
  EnvelopeReleaseActive,
  EnvelopeHandle,
  EnvelopeHandleActive,
  EnvelopeGrid,
  EnvelopeLabel,
  EnvelopeVoice,
  EnvelopeVoiceCursor,
  /// The Waveform window: the two areas' background and border, the
  /// oscilloscope's grid, its left and right channels and both once the
  /// output clips, and the spectrum's bars, grid and labels.
  WaveformBg,
  WaveformBorder,
  WaveformGrid,
  WaveformLeft,
  WaveformRight,
  WaveformClipped,
  SpectrumBar,
  SpectrumGrid,
  SpectrumLabel,
  /// The frame around the algorithm and SSG-EG pictures.
  PreviewBorder,
  /// Behind a Patch Editor window; the others take ImGuiCol_WindowBg.
  PatchEditorBg,
  /// The bordered panes inside a dialog, such as About's license list and
  /// text.
  DialogPaneBg,
  DialogPaneBorder,
  COUNT
};

struct MegatoyStyle {
  ImVec4 colors[static_cast<int>(MegatoyCol::COUNT)];
};

MegatoyStyle &mutable_style();
const MegatoyStyle &style();

/// Sets the operator, badge and envelope colors from the ImGui colors and
/// TextHighlight a theme has set so far. A theme calls it before setting any
/// of them itself.
void derive_widget_colors();

const ImVec4 &color(MegatoyCol col);
ImU32 color_u32(MegatoyCol col);

} // namespace ui::styles
