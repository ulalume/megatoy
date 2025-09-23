#pragma once
#include <array>
#include <cstdint>
#include "../types.hpp"
namespace ym2612 {

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

struct GlobalSettings {
  bool dac_enable;
  bool lfo_enable;
  uint8_t lfo_frequency; // 0 ~ 7
};

struct OperatorSettings {
  uint8_t attack_rate;               // 0 ~ 31
  uint8_t decay_rate;                // 0 ~ 31
  uint8_t sustain_rate;              // 0 ~ 31
  uint8_t release_rate;              // 0 ~ 15
  uint8_t sustain_level;             // 0 ~ 15
  uint8_t total_level;               // 0 ~ 127
  uint8_t key_scale;                 // 0 ~ 3
  uint8_t multiple;                  // 0 ~ 15
  uint8_t detune;                    // 0 ~ 7
  uint8_t ssg_type_envelope_control; // 0 ~ 7
  bool ssg_enable;
  bool amplitude_modulation_enable;
};

struct ChannelSettings {
  bool left_speaker = true;
  bool right_speaker = true;
  uint8_t amplitude_modulation_sensitivity; // 0 ~ 3 ams for lfo
  uint8_t frequency_modulation_sensitivity; // 0 ~ 7 pms for lfo
};

struct ChannelInstrument {
  uint8_t feedback;  // 0 ~ 7
  uint8_t algorithm; // 0 ~ 7
  OperatorSettings operators[4];
};

} // namespace ym2612
