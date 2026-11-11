#pragma once

#include "ym2612/operator_edit.hpp"
#include "ym2612/patch.hpp"
#include <array>
#include <cstdint>

namespace ym2612 {

/**
 * How a patch is played rather than the patch itself: the LFO's switch and
 * rate and which operators sound. One set serves whichever patch is current;
 * none of it is saved with a patch, which keeps these fields at their
 * defaults, and it is laid over the patch on its way to the chip. Both
 * speakers stay on, and the DAC off, since it would take channel 6 from FM.
 */
struct PlaybackSettings {
  bool lfo_enable = true;
  uint8_t lfo_frequency = 0;
  /// By display slot, OP1..OP4 as the editor shows them.
  std::array<bool, 4> operator_enable{true, true, true, true};

  bool operator==(const PlaybackSettings &) const = default;
};

/// Puts the LFO, speaker, operator enable and DAC fields at their defaults,
/// as every patch holds them.
inline void clear_playback_fields(Patch &patch) {
  const Patch defaults;
  patch.global = defaults.global;
  patch.channel.left_speaker = defaults.channel.left_speaker;
  patch.channel.right_speaker = defaults.channel.right_speaker;
  for (auto &op : patch.instrument.operators) {
    op.enable = true;
  }
}

/// `patch` as `settings` play it.
inline Patch played(const Patch &patch, const PlaybackSettings &settings) {
  Patch out = patch;
  out.global.dac_enable = false;
  out.global.lfo_enable = settings.lfo_enable;
  out.global.lfo_frequency = settings.lfo_frequency;
  out.channel.left_speaker = true;
  out.channel.right_speaker = true;
  for (int slot = 0; slot < 4; ++slot) {
    operator_at(out.instrument, slot).enable = settings.operator_enable[slot];
  }
  return out;
}

/// `patch` as written to a file: as `settings` play it, except that the LFO's
/// rate is named whether the LFO is on or not. Only the MML comment keeps
/// it, and only for a patch the LFO reaches.
inline Patch written(const Patch &patch, const PlaybackSettings &settings) {
  Patch out = played(patch, settings);
  out.global.lfo_enable = true;
  return out;
}

} // namespace ym2612
