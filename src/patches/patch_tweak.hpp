#pragma once

#include "ym2612/types.hpp"
#include <array>
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

/// The envelope fields of one operator that a tweak moves.
struct TweakFields {
  bool total_level = false;
  bool attack_rate = false;
  bool decay_rate = false;
  bool sustain_level = false;
  bool sustain_rate = false;
  bool release_rate = false;
};

/// What moving `tweak` changes on each operator, in storage order; nothing
/// when the tweak has no value.
std::array<TweakFields, 4>
tweak_fields(const ym2612::ChannelInstrument &instrument, Tweak tweak);

/**
 * The envelope the tweaks read, rough enough to sketch: each part from 0 to 1.
 * `level` is how loud; `attack`, `decay` and `release` how long each takes;
 * `sustain` how much is left once the decay is over, and `hold` how fast that
 * fades while the key is held.
 */
struct TweakShape {
  float level = 0.0f;
  float attack = 0.0f;
  float decay = 0.0f;
  float sustain = 0.0f;
  float hold = 0.0f;
  float release = 0.0f;
};

/// None when no carrier is enabled.
std::optional<TweakShape>
tweak_shape(const ym2612::ChannelInstrument &instrument);

/// The value as the registers know it, from the operator it is read from.
std::string tweak_text(const ym2612::ChannelInstrument &instrument,
                       Tweak tweak);

/// How far a drag from `from` has taken the tweak to `to`: in dB for Volume,
/// which every sounding carrier moves by alike; as tweak_text() otherwise.
std::string tweak_change_text(const ym2612::ChannelInstrument &from,
                              const ym2612::ChannelInstrument &to, Tweak tweak);

} // namespace patch_lab
