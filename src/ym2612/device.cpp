#include "device.hpp"
#include "channel.hpp"
#include <iostream>

namespace ym2612 {
Device::Device() {}
void Device::init(uint32_t smplRate) {
  config.emuCore = FCC_GPGX;
  config.srMode = DEVRI_SRMODE_CUSTOM;
  config.flags = 0x00;
  config.clock = 7670454;
  config.smplRate = smplRate;

  auto error_code = SndEmu_Start(DEVID_YM2612, &config, &info);
  if (error_code) {
    std::cout << "failed to init YM2612" << std::endl;
  }

  info.devDef->SetOptionBits(info.dataPtr, 0x80);

  SndEmu_GetDeviceFunc(info.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0,
                       (void **)&device_func_write);

  info.devDef->Reset(info.dataPtr);
}
Device::~Device() { stop(); }

void Device::write(uint8_t reg, uint8_t data, bool port) {
  if (info.dataPtr == nullptr || device_func_write == nullptr) {
    return;
  }
  device_func_write(info.dataPtr, (port << 1), reg);      // Register address
  device_func_write(info.dataPtr, (port << 1) + 1, data); // Data payload
}

void Device::write_settings(const GlobalSettings &settings) {
  write(0x2B, settings.dac_enable << 7);
  write(0x22, settings.lfo_enable << 3 | settings.lfo_frequency);
}

void Device::update(uint32_t sample_buffer, std::array<DEV_SMPL *, 2> &outs) {
  if (info.dataPtr != nullptr && info.devDef != nullptr) {
    info.devDef->Update(info.dataPtr, sample_buffer, outs.data());
  }
}

void Device::stop() {
  if (info.dataPtr != nullptr && info.devDef != nullptr) {
    SndEmu_Stop(&info);
    info.dataPtr = nullptr;
    info.devDef = nullptr;
  }
}

Channel Device::channel(ChannelIndex idx) { return Channel(*this, idx); }

} // namespace ym2612
