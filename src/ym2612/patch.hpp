#pragma once
#include "ym2612/types.hpp"
#include <algorithm>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>

namespace ym2612 {

struct Patch {
  std::string name = "";

  GlobalSettings global;
  ChannelSettings channel;
  ChannelInstrument instrument;
};

inline bool operator==(const GlobalSettings &lhs, const GlobalSettings &rhs) {
  return lhs.dac_enable == rhs.dac_enable && lhs.lfo_enable == rhs.lfo_enable &&
         lhs.lfo_frequency == rhs.lfo_frequency;
}

inline bool operator!=(const GlobalSettings &lhs, const GlobalSettings &rhs) {
  return !(lhs == rhs);
}

inline bool operator==(const OperatorSettings &lhs,
                       const OperatorSettings &rhs) {
  return lhs.attack_rate == rhs.attack_rate &&
         lhs.decay_rate == rhs.decay_rate &&
         lhs.sustain_rate == rhs.sustain_rate &&
         lhs.release_rate == rhs.release_rate &&
         lhs.sustain_level == rhs.sustain_level &&
         lhs.total_level == rhs.total_level && lhs.key_scale == rhs.key_scale &&
         lhs.multiple == rhs.multiple && lhs.detune == rhs.detune &&
         lhs.ssg_type_envelope_control == rhs.ssg_type_envelope_control &&
         lhs.ssg_enable == rhs.ssg_enable &&
         lhs.amplitude_modulation_enable == rhs.amplitude_modulation_enable &&
         lhs.enable == rhs.enable;
}

inline bool operator!=(const OperatorSettings &lhs,
                       const OperatorSettings &rhs) {
  return !(lhs == rhs);
}

inline bool operator==(const ChannelSettings &lhs, const ChannelSettings &rhs) {
  return lhs.left_speaker == rhs.left_speaker &&
         lhs.right_speaker == rhs.right_speaker &&
         lhs.amplitude_modulation_sensitivity ==
             rhs.amplitude_modulation_sensitivity &&
         lhs.frequency_modulation_sensitivity ==
             rhs.frequency_modulation_sensitivity;
}

inline bool operator!=(const ChannelSettings &lhs, const ChannelSettings &rhs) {
  return !(lhs == rhs);
}

inline bool operator==(const ChannelInstrument &lhs,
                       const ChannelInstrument &rhs) {
  return lhs.feedback == rhs.feedback && lhs.algorithm == rhs.algorithm &&
         std::equal(std::begin(lhs.operators), std::end(lhs.operators),
                    std::begin(rhs.operators), std::end(rhs.operators));
}

inline bool operator!=(const ChannelInstrument &lhs,
                       const ChannelInstrument &rhs) {
  return !(lhs == rhs);
}

inline bool operator==(const Patch &lhs, const Patch &rhs) {
  return lhs.name == rhs.name && lhs.global == rhs.global &&
         lhs.channel == rhs.channel && lhs.instrument == rhs.instrument;
}

inline bool operator!=(const Patch &lhs, const Patch &rhs) {
  return !(lhs == rhs);
}

// JSON serialization helpers
inline void to_json(nlohmann::json &j, const GlobalSettings &device) {
  j = nlohmann::json{{"dac_enable", device.dac_enable},
                     {"lfo_enable", device.lfo_enable},
                     {"lfo_frequency", device.lfo_frequency}};
}

inline void from_json(const nlohmann::json &j, GlobalSettings &device) {
  j.at("dac_enable").get_to(device.dac_enable);
  j.at("lfo_enable").get_to(device.lfo_enable);
  j.at("lfo_frequency").get_to(device.lfo_frequency);
}

inline void to_json(nlohmann::json &j, const ChannelSettings &channel) {
  j = nlohmann::json{{"left_speaker", channel.left_speaker},
                     {"right_speaker", channel.right_speaker},
                     {"amplitude_modulation_sensitivity",
                      channel.amplitude_modulation_sensitivity},
                     {"frequency_modulation_sensitivity",
                      channel.frequency_modulation_sensitivity}};
}

inline void from_json(const nlohmann::json &j, ChannelSettings &channel) {
  j.at("left_speaker").get_to(channel.left_speaker);
  j.at("right_speaker").get_to(channel.right_speaker);
  j.at("amplitude_modulation_sensitivity")
      .get_to(channel.amplitude_modulation_sensitivity);
  j.at("frequency_modulation_sensitivity")
      .get_to(channel.frequency_modulation_sensitivity);
}

inline void to_json(nlohmann::json &j, const OperatorSettings &op) {
  j = nlohmann::json{
      {"attack_rate", op.attack_rate},
      {"decay_rate", op.decay_rate},
      {"sustain_rate", op.sustain_rate},
      {"release_rate", op.release_rate},
      {"sustain_level", op.sustain_level},
      {"total_level", op.total_level},
      {"key_scale", op.key_scale},
      {"multiple", op.multiple},
      {"detune", op.detune},
      {"ssg_type_envelope_control", op.ssg_type_envelope_control},
      {"ssg_enable", op.ssg_enable},
      {"amplitude_modulation_enable", op.amplitude_modulation_enable},
      {"enable", op.enable},
  };
}

inline void from_json(const nlohmann::json &j, OperatorSettings &op) {
  j.at("attack_rate").get_to(op.attack_rate);
  j.at("decay_rate").get_to(op.decay_rate);
  j.at("sustain_rate").get_to(op.sustain_rate);
  j.at("release_rate").get_to(op.release_rate);
  j.at("sustain_level").get_to(op.sustain_level);
  j.at("total_level").get_to(op.total_level);
  j.at("key_scale").get_to(op.key_scale);
  j.at("multiple").get_to(op.multiple);
  j.at("detune").get_to(op.detune);
  j.at("ssg_type_envelope_control").get_to(op.ssg_type_envelope_control);
  j.at("ssg_enable").get_to(op.ssg_enable);
  j.at("amplitude_modulation_enable").get_to(op.amplitude_modulation_enable);
  if (j.contains("enable")) {
    j.at("enable").get_to(op.enable);
  } else {
    op.enable = true;
  }
}

inline void to_json(nlohmann::json &j, const ChannelInstrument &instrument) {
  j = nlohmann::json{{"feedback", instrument.feedback},
                     {"algorithm", instrument.algorithm},
                     {"operators", instrument.operators}};
}

inline void from_json(const nlohmann::json &j, ChannelInstrument &instrument) {
  j.at("feedback").get_to(instrument.feedback);
  j.at("algorithm").get_to(instrument.algorithm);
  auto ops = j.at("operators");
  for (size_t i = 0; i < 4; ++i) {
    ops[i].get_to(instrument.operators[i]);
  }
}

inline void to_json(nlohmann::json &j, const Patch &patch) {
  j = nlohmann::json{{"name", patch.name},
                     {"device", patch.global},
                     {"channel", patch.channel},
                     {"instrument", patch.instrument}};
}

inline void from_json(const nlohmann::json &j, Patch &patch) {
  j.at("name").get_to(patch.name);
  j.at("device").get_to(patch.global);
  j.at("channel").get_to(patch.channel);
  j.at("instrument").get_to(patch.instrument);
}

} // namespace ym2612
