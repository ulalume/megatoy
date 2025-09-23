#include "channel.hpp"
#include "device.hpp"
#include "operator.hpp"

namespace ym2612 {

void Channel::write_settings(const ChannelSettings &settings) {
  const auto &[value, port] = channel_index_to_value(index);
  device.write(0xB4 + value,
               settings.left_speaker << 7 | settings.right_speaker << 6 |
                   settings.amplitude_modulation_sensitivity << 4 |
                   settings.frequency_modulation_sensitivity,
               port);
}

void Channel::write_instrument(const ChannelInstrument &instrument) {
  const auto &[value, port] = channel_index_to_value(index);
  device.write(0xB0 + value, instrument.feedback << 3 | instrument.algorithm,
               port);
  for (OperatorIndex op_index : all_operator_indices) {
    op(op_index).write_settings(
        instrument.operators[static_cast<uint8_t>(op_index)]);
  }
}

void Channel::write_frequency(const Note &note) {
  const auto &[octave, key] = note;
  const auto fnote = fnote_from_key(key);
  const auto &[value, port] = channel_index_to_value(index);
  device.write(0xA4 + value, (octave << 3) | ((fnote >> 8) & 0x07), port);
  device.write(0xA0 + value, fnote & 0xFF, port);
}

void Channel::write_key_on() {
  // YM2612 Key On/Off uses specific channel values: 0,1,2,4,5,6
  uint8_t key_channel = static_cast<uint8_t>(index);
  if (key_channel >= 3) {
    key_channel += 1; // CH4=4, CH5=5, CH6=6
  }
  device.write(0x28, 0b1111 << 4 | key_channel, false);
}

void Channel::write_key_off() {
  // YM2612 Key On/Off uses specific channel values: 0,1,2,4,5,6
  uint8_t key_channel = static_cast<uint8_t>(index);
  if (key_channel >= 3) {
    key_channel += 1; // CH4=4, CH5=5, CH6=6
  }
  device.write(0x28, 0b0000 << 4 | key_channel, false);
}

Operator Channel::op(OperatorIndex idx) { return Operator(device, index, idx); }

} // namespace ym2612
