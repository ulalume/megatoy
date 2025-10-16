#include "audio/audio_runtime.hpp"

#include <array>
#include <iostream>

AudioRuntime::AudioRuntime(AudioManager &audio_manager, ym2612::Device &device,
                           ym2612::WaveSampler &wave_sampler)
    : audio_manager_(audio_manager), device_(device),
      wave_sampler_(wave_sampler), sample_rate_(0) {}

bool AudioRuntime::start(UINT32 target_sample_rate) {
  wave_sampler_.clear();

  const bool audio_ready = audio_manager_.init(target_sample_rate);
  const UINT32 device_sample_rate =
      audio_ready ? audio_manager_.get_sample_rate() : target_sample_rate;

  device_.stop();
  device_.init(device_sample_rate);

  audio_manager_.clear_callback();
  audio_manager_.set_callback(
      [this](UINT32 sample_count, std::array<DEV_SMPL *, 2> &outputs) {
        device_.update(sample_count, outputs);
        wave_sampler_.push_samples(outputs[0], outputs[1], sample_count);
      });

  sample_rate_ = device_sample_rate;

  if (!audio_ready) {
    std::cerr << "Failed to initialize audio system\n";
    audio_manager_.clear_callback();
    return false;
  }

  if (device_sample_rate != target_sample_rate) {
    std::cout << "Audio sample rate set to " << device_sample_rate << " Hz\n";
  }

  if (!audio_manager_.start()) {
    std::cerr << "Failed to start audio streaming\n";
    audio_manager_.clear_callback();
    return false;
  }

  return true;
}

void AudioRuntime::stop() {
  if (audio_manager_.is_running()) {
    audio_manager_.stop();
  }
  audio_manager_.clear_callback();
  device_.stop();
  wave_sampler_.clear();
}
