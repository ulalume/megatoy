#pragma once

#include "types.hpp"
#include <emu/EmuCores.h>
#include <emu/EmuStructs.h>
#include <emu/SoundDevs.h>
#include <emu/SoundEmu.h>
#include <stdlib.h>

namespace ym2612 {

class Channel; // Forward declaration

class Device {
public:
  Device();
  void init(uint32_t smplRate);
  ~Device();

  Channel channel(ChannelIndex idx);

  void write(uint8_t reg, uint8_t data, bool port = false);
  void write_settings(const GlobalSettings &settings);

  void update(uint32_t sample_buffer, std::array<DEV_SMPL *, 2> &outs);
  void stop();

private:
  DEVFUNC_WRITE_A8D8 device_func_write = nullptr;
  DEV_GEN_CFG config;
  DEV_INFO info;
  bool is_initialized() const;
};

} // namespace ym2612
