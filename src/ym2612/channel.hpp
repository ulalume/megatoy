#pragma once
#include "note.hpp"
#include "types.hpp"
#include <emu/EmuCores.h>
#include <emu/EmuStructs.h>
#include <emu/SoundDevs.h>
#include <emu/SoundEmu.h>

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
  void write_key_on();
  void write_key_off();

private:
  Device &device;
  ChannelIndex index;
};

} // namespace ym2612
