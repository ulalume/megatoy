#include "ym2612/device.hpp"
#include "ym2612/channel.hpp"

#include <cstring>
#include <vector>

extern "C" {
#include "audio/resampler/Resampler.h"
}

namespace ym2612 {

namespace {

// The resampler applies an 8.8 fixed-point gain, so a full-scale chip sample
// (YmfmChip::kFullScale) comes out as kFullScale * kResamplerVolume.
constexpr uint16_t kResamplerVolume = 0x100;
constexpr float kInverseFullScale =
    1.0f / (static_cast<float>(YmfmChip::kFullScale) * kResamplerVolume);

} // namespace

struct Device::Resampler {
  RESMPL_STATE state{};
  bool initialized = false;
  std::vector<WAVE_32BS> scratch;

  ~Resampler() { deinit(); }

  void deinit() {
    if (initialized) {
      Resmpl_Deinit(&state);
      initialized = false;
    }
    state = {};
  }

  void init(YmfmChip &chip, uint32_t destination_rate) {
    deinit();
    state.smpRateSrc = chip.native_sample_rate();
    state.StreamUpdate = &YmfmChip::stream_update;
    state.su_DataPtr = &chip;
    Resmpl_SetVals(&state, 0xFF, kResamplerVolume, destination_rate);
    Resmpl_Init(&state);
    initialized = true;
  }
};

Device::Device() = default;
Device::~Device() { stop(); }

void Device::init(uint32_t sample_rate) {
  stop();
  if (sample_rate == 0) {
    return;
  }

  sample_rate_ = sample_rate;
  chip_ = std::make_unique<YmfmChip>(kClock);
  resampler_ = std::make_unique<Resampler>();
  resampler_->init(*chip_, sample_rate_);
}

void Device::stop() {
  resampler_.reset();
  chip_.reset();
  sample_rate_ = 0;
}

uint32_t Device::native_sample_rate() const {
  return chip_ ? chip_->native_sample_rate() : 0;
}

void Device::write(uint8_t reg, uint8_t data, bool port) {
  if (!chip_) {
    return;
  }
  const uint8_t offset = static_cast<uint8_t>(static_cast<uint8_t>(port) << 1);
  chip_->write(offset, reg);                            // register address
  chip_->write(static_cast<uint8_t>(offset + 1), data); // data payload
}

void Device::write_settings(const GlobalSettings &settings) {
  write(0x2B, static_cast<uint8_t>(settings.dac_enable << 7));
  write(0x22, static_cast<uint8_t>(settings.lfo_enable << 3 |
                                   settings.lfo_frequency));
}

void Device::render(uint32_t frames, float *out) {
  if (out == nullptr || frames == 0) {
    return;
  }
  if (!chip_ || !resampler_ || !resampler_->initialized) {
    std::memset(out, 0, static_cast<size_t>(frames) * 2 * sizeof(float));
    return;
  }

  auto &scratch = resampler_->scratch;
  if (scratch.size() < frames) {
    scratch.resize(frames);
  }
  // Resmpl_Execute accumulates into the buffer, so it must start silent.
  std::memset(scratch.data(), 0,
              static_cast<size_t>(frames) * sizeof(WAVE_32BS));

  Resmpl_Execute(&resampler_->state, frames, scratch.data());

  for (uint32_t i = 0; i < frames; ++i) {
    out[i * 2 + 0] = static_cast<float>(scratch[i].L) * kInverseFullScale;
    out[i * 2 + 1] = static_cast<float>(scratch[i].R) * kInverseFullScale;
  }
}

Channel Device::channel(ChannelIndex idx) { return Channel(*this, idx); }

} // namespace ym2612
