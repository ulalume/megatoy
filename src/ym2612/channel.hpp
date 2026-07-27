#pragma once
#include "note.hpp"
#include "ym2612/types.hpp"

namespace ym2612 {

class Device;   // Forward declaration
class Operator; // Forward declaration

class Channel {
public:
  Channel(Device &device, ChannelIndex index) : device(device), index(index) {}

  Operator op(OperatorIndex idx);

  void write_settings(const ChannelSettings &settings);
  void write_instrument(const ChannelInstrument &instrument);
  void write_frequency(const Note &note);
  void write_key_on(bool op1, bool op2, bool op3, bool op4);
  void write_key_off();

private:
  Device &device;
  ChannelIndex index;
};

} // namespace ym2612
