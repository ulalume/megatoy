#include "operator.hpp"
#include "device.hpp"
#include "core/types.hpp"

namespace ym2612 {

void Operator::write_settings(const OperatorSettings &settings) {
  const auto &[value, port] = channel_index_to_value(channel_index);
  const auto c_o = value + (static_cast<uint16_t>(operator_index) << 2);
  device.write(0x30 + c_o, settings.detune << 4 | settings.multiple, port);
  device.write(0x40 + c_o, settings.total_level, port);
  device.write(0x50 + c_o, settings.key_scale << 6 | settings.attack_rate,
               port);
  device.write(0x60 + c_o,
               settings.amplitude_modulation_enable << 7 | settings.decay_rate,
               port);
  device.write(0x70 + c_o, settings.sustain_rate, port);
  device.write(0x80 + c_o, settings.sustain_level << 4 | settings.release_rate,
               port);
  device.write(0x90 + c_o,
               settings.ssg_enable << 3 | settings.ssg_type_envelope_control,
               port);
}

} // namespace ym2612
