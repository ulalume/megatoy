#pragma once

#include "ym2612/types.hpp"
#include <optional>
#include <string>

namespace patch_lab {

/**
 * A broad edit to a whole instrument. Its value is read from the operator that
 * decides what is heard, and moving it shifts every operator concerned by the
 * same amount, keeping the differences between them.
 */
enum class Tweak {
  Volume,
  Brightness,
  Attack,
  Decay,
  Release,
};

struct TweakRange {
  int min = 0;
  int max = 0;
};

/// The value now; none when the instrument has no enabled operator to move.
std::optional<int> tweak_value(const ym2612::ChannelInstrument &instrument,
                               Tweak tweak);

TweakRange tweak_range(const ym2612::ChannelInstrument &instrument,
                       Tweak tweak);

/// `base` moved as close to `value` as its operators can go. Calculating from
/// one base keeps a move reversible past the point where an operator stops.
ym2612::ChannelInstrument with_tweak(const ym2612::ChannelInstrument &base,
                                     Tweak tweak, int value);

/// The value as the registers or the ear know it.
std::string tweak_text(const ym2612::ChannelInstrument &instrument,
                       Tweak tweak);

} // namespace patch_lab
