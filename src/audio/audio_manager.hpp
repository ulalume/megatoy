#pragma once

#include "ym2612/device.hpp"
#include "ym2612/patch.hpp"
#include "ym2612/wave_sampler.hpp"
#include <SDL3/SDL.h>
#include <emu/EmuStructs.h>
#include <atomic>
#include <stop_token>
#include <thread>
#include <vector>

/**
 * AudioManager - audio system management
 */
class AudioManager {
public:
  AudioManager();
  ~AudioManager();

  // Non-copyable, non-movable
  AudioManager(const AudioManager &) = delete;
  AudioManager &operator=(const AudioManager &) = delete;
  AudioManager(AudioManager &&) = delete;
  AudioManager &operator=(AudioManager &&) = delete;

  /**
   * Initialize and start the complete audio system
   * @param sample_rate Target sample rate
   * @return true on success, false on failure
   */
  bool initialize(UINT32 sample_rate);

  /**
   * Shutdown and cleanup complete audio system
   */
  void shutdown();

  /**
   * Direct access to YM2612 device
   */
  ym2612::Device &device() { return device_; }
  const ym2612::Device &device() const { return device_; }

  /**
   * Direct access to wave sampler
   */
  ym2612::WaveSampler &wave_sampler() { return wave_sampler_; }
  const ym2612::WaveSampler &wave_sampler() const { return wave_sampler_; }

  /**
   * Apply patch settings to all channels
   */
  void apply_patch_to_all_channels(const ym2612::Patch &patch);

  /**
   * Check if audio system is running
   */
  bool is_running() const { return running_; }

  /**
   * Get current sample rate
   */
  UINT32 sample_rate() const { return sample_rate_; }

private:
  void audio_thread_func(std::stop_token stop_token);
  void pump_audio_stream();

  // Instance callback implementation
  UINT32 fill_buffer(UINT32 buf_size, void *data);

  // Core audio system
  SDL_AudioStream *audio_stream_;
  bool owns_audio_subsystem_;
  std::jthread audio_thread_;
  std::atomic<bool> audio_thread_alive_{false};
  UINT32 smpl_size_;
  UINT32 smpl_alloc_;
  UINT32 sample_rate_;
  UINT32 target_buffer_bytes_;

  // Sample buffers
  std::vector<DEV_SMPL> smpl_data_[2];
  std::vector<INT16> stream_buffer_;

  // YM2612 device and wave sampler
  ym2612::Device device_;
  ym2612::WaveSampler wave_sampler_;

  // State flags
  bool initialized_;
  bool running_;
};
