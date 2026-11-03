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
  /// The envelope graph: the curve, the stretch a hovered or dragged slider
  /// sets, the grid and frame, the captions, the curves of sounding voices
  /// and their cursors.
  EnvelopeCurve,
  EnvelopeCurveActive,
  EnvelopeGrid,
  EnvelopeLabel,
  EnvelopeVoice,
  EnvelopeVoiceCursor,
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
