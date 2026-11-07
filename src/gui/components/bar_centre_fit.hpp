#pragma once

namespace ui {

/// How much of what sits in the middle of the menu bar there is room for.
enum class BarCentreFit {
  /// All of it.
  Full,
  /// Without the note beside it.
  Short,
  /// Its icon alone.
  Icon,
};

/// What sits in the middle of the menu bar, at one fit: how wide it is, and
/// how much of that, from its left, is centred in the bar.
struct BarCentreSize {
  float centred = 0.0f;
  float total = 0.0f;
};

} // namespace ui
