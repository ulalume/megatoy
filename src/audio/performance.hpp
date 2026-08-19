#pragma once

#include "ym2612/types.hpp"

#include <algorithm>
#include <cstdint>

namespace audio::performance {

inline constexpr uint16_t kPitchBendCenter = 8192;
inline constexpr uint16_t kPitchBendMaximum = 16383;
inline constexpr float kPitchBendRangeSemitones = 2.0f;
inline constexpr uint8_t kWheelOnlyLfoFrequency = 2; // 6.02 Hz

inline constexpr float pitch_bend_semitones(uint16_t value) {
  const auto clamped = std::min(value, kPitchBendMaximum);
  return (static_cast<int>(clamped) - static_cast<int>(kPitchBendCenter)) *
         (kPitchBendRangeSemitones / kPitchBendCenter);
}

inline constexpr uint8_t wheel_pms(uint8_t wheel) {
  return wheel == 0 ? 0 : static_cast<uint8_t>(1 + (wheel * 6) / 127);
}

inline constexpr uint8_t effective_velocity(bool use_velocity,
                                            uint8_t velocity) {
  return use_velocity ? static_cast<uint8_t>(velocity & 0x7f) : 127;
}

inline ym2612::ChannelSettings
compose_channel_settings(const ym2612::ChannelSettings &patch_settings,
                         bool patch_lfo_enabled, uint8_t wheel) {
  auto effective = patch_settings;
  effective.frequency_modulation_sensitivity =
      std::max(effective.frequency_modulation_sensitivity, wheel_pms(wheel));
  if (!patch_lfo_enabled) {
    effective.amplitude_modulation_sensitivity = 0;
  }
  return effective;
}

inline ym2612::GlobalSettings
compose_global_settings(const ym2612::GlobalSettings &patch_settings,
                        uint8_t wheel) {
  auto effective = patch_settings;
  if (!patch_settings.lfo_enable && wheel_pms(wheel) > 0) {
    effective.lfo_enable = true;
    effective.lfo_frequency = kWheelOnlyLfoFrequency;
  }
  return effective;
}

} // namespace audio::performance
