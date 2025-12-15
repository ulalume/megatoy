#pragma once

#include "ym2612/device.hpp"
#include "ym2612/patch.hpp"
#include "ym2612/wave_sampler.hpp"
#include <emu/EmuStructs.h>
#include <vector>

class AudioEngine {
public:
  AudioEngine();
  ~AudioEngine() = default;

  AudioEngine(const AudioEngine &) = delete;
  AudioEngine &operator=(const AudioEngine &) = delete;
  AudioEngine(AudioEngine &&) = delete;
  AudioEngine &operator=(AudioEngine &&) = delete;

  bool initialize(UINT32 sample_rate);
  void shutdown();

  UINT32 render(UINT32 buf_size, void *data);

  void apply_patch_to_all_channels(const ym2612::Patch &patch);

  ym2612::Device &device() { return device_; }
  const ym2612::Device &device() const { return device_; }

  ym2612::WaveSampler &wave_sampler() { return wave_sampler_; }
  const ym2612::WaveSampler &wave_sampler() const { return wave_sampler_; }

  UINT32 sample_rate() const { return sample_rate_; }
  UINT32 frame_size() const { return frame_size_; }
  bool is_running() const { return running_; }

private:
  UINT32 ensure_sample_storage(UINT32 smpl_count);

  UINT32 sample_rate_;
  UINT32 frame_size_;
  UINT32 sample_capacity_;

  std::vector<DEV_SMPL> smpl_data_[2];
  ym2612::Device device_;
  ym2612::WaveSampler wave_sampler_;
  bool running_;
};
