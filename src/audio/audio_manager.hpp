#pragma once

#include "audio/audio_engine.hpp"
#include "audio/audio_transport.hpp"
#include "ym2612/patch.hpp"
#include <cstdint>
#include <memory>

/**
 * AudioManager - audio system management
 */
class AudioManager {
public:
  AudioManager();
  explicit AudioManager(std::unique_ptr<AudioTransport> transport);
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
  bool initialize(uint32_t sample_rate);

  /**
   * Shutdown and cleanup complete audio system
   */
  void shutdown();

  /**
   * Direct access to YM2612 device
   */
  ym2612::Device &device() { return engine_.device(); }
  const ym2612::Device &device() const { return engine_.device(); }

  /**
   * Direct access to the scope buffer holding recent output
   */
  audio::ScopeBuffer &scope_buffer() { return engine_.scope_buffer(); }
  const audio::ScopeBuffer &scope_buffer() const {
    return engine_.scope_buffer();
  }

  /**
   * Apply patch settings to all channels
   */
  void apply_patch_to_all_channels(const ym2612::Patch &patch);

  /**
   * Check if audio system is running
   */
  bool is_running() const { return engine_.is_running(); }

  /**
   * Get current sample rate
   */
  uint32_t sample_rate() const { return engine_.sample_rate(); }

private:
  AudioEngine engine_;
  std::unique_ptr<AudioTransport> transport_;
};
