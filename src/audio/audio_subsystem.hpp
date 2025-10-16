#pragma once

#include "audio/audio_manager.hpp"
#include "audio/audio_runtime.hpp"
#include "ym2612/device.hpp"
#include "ym2612/patch.hpp"
#include "ym2612/wave_sampler.hpp"

class AudioSubsystem {
public:
  AudioSubsystem();

  bool initialize(UINT32 sample_rate);
  void shutdown();

  ym2612::Device &device() { return device_; }
  const ym2612::Device &device() const { return device_; }

  ym2612::WaveSampler &wave_sampler() { return wave_sampler_; }
  const ym2612::WaveSampler &wave_sampler() const { return wave_sampler_; }

  AudioManager &manager() { return audio_manager_; }
  const AudioManager &manager() const { return audio_manager_; }

  void apply_patch_to_all_channels(const ym2612::Patch &patch);

private:
  AudioManager audio_manager_;
  ym2612::Device device_;
  ym2612::WaveSampler wave_sampler_;
  AudioRuntime runtime_;
};
