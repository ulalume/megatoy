#pragma once
#include "core/types.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
namespace ym2612 {

const uint8_t algorithm_modulator_count[8] = {3, 3, 3, 3, 2, 1, 1, 0};

// GM2/DLS attenuation in YM2612 TL steps (0.75 dB each):
// round(40 * log10(127 / velocity) / 0.75), clamped to the register range.
inline constexpr std::array<uint8_t, 128> velocity_tl_offset = {
    127, 112, 96, 87, 80, 75, 71, 67, 64, 61, 59, 57, 55, 53, 51, 49,
    48,  47,  45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 33,
    32,  31,  31, 30, 29, 29, 28, 27, 27, 26, 26, 25, 25, 24, 24, 23,
    23,  22,  22, 21, 21, 20, 20, 19, 19, 19, 18, 18, 17, 17, 17, 16,
    16,  16,  15, 15, 14, 14, 14, 13, 13, 13, 13, 12, 12, 12, 11, 11,
    11,  10,  10, 10, 10, 9,  9,  9,  8,  8,  8,  8,  7,  7,  7,  7,
    6,   6,   6,  6,  6,  5,  5,  5,  5,  4,  4,  4,  4,  4,  3,  3,
    3,   3,   3,  2,  2,  2,  2,  2,  1,  1,  1,  1,  1,  0,  0,  0};

enum class ChannelIndex : uint8_t {
  Fm1 = 0,
  Fm2 = 1,
  Fm3 = 2,
  Fm4 = 3,
  Fm5 = 4,
  Fm6 = 5,
};

inline std::pair<uint16_t, bool> channel_index_to_value(ChannelIndex index) {
  auto i = static_cast<uint16_t>(index);
  if (i < 3) {
    return {i, false};
  }
  return {i % 3, true};
}

inline const std::array<ChannelIndex, 6> all_channel_indices = {
    ChannelIndex::Fm1, ChannelIndex::Fm2, ChannelIndex::Fm3,
    ChannelIndex::Fm4, ChannelIndex::Fm5, ChannelIndex::Fm6,
};

enum class OperatorIndex : uint8_t {
  Op1 = 0,
  Op2 = 2,
  Op3 = 1,
  Op4 = 3,
};

inline const std::array<OperatorIndex, 4> all_operator_indices = {
    OperatorIndex::Op1,
    OperatorIndex::Op2,
    OperatorIndex::Op3,
    OperatorIndex::Op4,
};

// Every field carries a default so a default-constructed Patch is a valid,
// reproducible patch rather than indeterminate memory.
struct GlobalSettings {
  bool dac_enable = false;
  bool lfo_enable = false;
  uint8_t lfo_frequency = 0; // 0 ~ 7
};

struct OperatorSettings {
  uint8_t attack_rate = 0;               // 0 ~ 31
  uint8_t decay_rate = 0;                // 0 ~ 31
  uint8_t sustain_rate = 0;              // 0 ~ 31
  uint8_t release_rate = 0;              // 0 ~ 15
  uint8_t sustain_level = 0;             // 0 ~ 15
  uint8_t total_level = 0;               // 0 ~ 127
  uint8_t key_scale = 0;                 // 0 ~ 3
  uint8_t multiple = 0;                  // 0 ~ 15
  uint8_t detune = 0;                    // 0 ~ 7
  uint8_t ssg_type_envelope_control = 0; // 0 ~ 7
  bool ssg_enable = false;
  bool amplitude_modulation_enable = false;

  bool enable = true; // global register
};

struct ChannelSettings {
  bool left_speaker = true;
  bool right_speaker = true;
  uint8_t amplitude_modulation_sensitivity = 0; // 0 ~ 3 ams for lfo
  uint8_t frequency_modulation_sensitivity = 0; // 0 ~ 7 pms for lfo
};

struct ChannelInstrument {
  uint8_t feedback = 0;  // 0 ~ 7
  uint8_t algorithm = 0; // 0 ~ 7
  OperatorSettings operators[4]{};

  inline ym2612::ChannelInstrument
  clone_with_velocity(uint8_t velocity, uint8_t depth_percent) const {
    const int delta =
        (velocity_tl_offset[velocity & 0x7f] * depth_percent) / 100;
    ym2612::ChannelInstrument modified = *this;
    // change carrier's total level
    auto modulater_count = algorithm_modulator_count[modified.algorithm];
    for (int op = 3; op >= modulater_count; op--) {
      const int total_level = modified.operators[op].total_level + delta;
      modified.operators[op].total_level =
          static_cast<uint8_t>(std::min(total_level, 127));
    }
    return modified;
  }
};

} // namespace ym2612
