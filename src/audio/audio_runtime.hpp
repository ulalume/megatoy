#pragma once

#include "audio/audio_manager.hpp"
#include "ym2612/device.hpp"
#include "ym2612/wave_sampler.hpp"

#include <cstdint>

class AudioRuntime {
public:
  AudioRuntime(AudioManager &audio_manager, ym2612::Device &device,
               ym2612::WaveSampler &wave_sampler);

  bool start(UINT32 target_sample_rate);
  void stop();

  UINT32 sample_rate() const { return sample_rate_; }

private:
  AudioManager &audio_manager_;
  ym2612::Device &device_;
  ym2612::WaveSampler &wave_sampler_;
  UINT32 sample_rate_;
};
