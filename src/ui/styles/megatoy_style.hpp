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
  COUNT
};

struct MegatoyStyle {
  ImVec4 colors[static_cast<int>(MegatoyCol::COUNT)];
};

MegatoyStyle &mutable_style();
const MegatoyStyle &style();

const ImVec4 &color(MegatoyCol col);
ImU32 color_u32(MegatoyCol col);

} // namespace ui::styles
