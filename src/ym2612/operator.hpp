#pragma once
#include "types.hpp"
#include <emu/EmuCores.h>
#include <emu/EmuStructs.h>
#include <emu/SoundDevs.h>
#include <emu/SoundEmu.h>
#include <sys/signal.h>

namespace ym2612 {

class Device; // Forward declaration

class Operator {
public:
  Operator(Device &device, ChannelIndex channel_index,
           OperatorIndex operator_index)
      : device(device), channel_index(channel_index),
        operator_index(operator_index) {}

  void write_settings(const OperatorSettings &settings);

private:
  Device &device;
  ChannelIndex channel_index;
  OperatorIndex operator_index;
};

} // namespace ym2612
