#pragma once

#include <array>
#include <audio/AudioStream.h>
#include <emu/EmuStructs.h>
#include <functional>
#include <vector>

/**
 * AudioManager - Manages real-time audio output using libvgm audio system
 *
 * This class encapsulates all audio-related functionality including:
 * - Audio system initialization and cleanup
 * - Audio driver selection and management
 * - Real-time audio streaming with callback mechanism
 * - Sample buffer management
 */
class AudioManager {
public:
  // Callback function type for audio generation
  // Parameters: sampleCount, outputBuffers[left, right]
  using AudioCallback = std::function<void(UINT32 sample_count,
                                           std::array<DEV_SMPL *, 2> &outputs)>;

  AudioManager();
  ~AudioManager();

  // Non-copyable, non-movable
  AudioManager(const AudioManager &) = delete;
  AudioManager &operator=(const AudioManager &) = delete;
  AudioManager(AudioManager &&) = delete;
  AudioManager &operator=(AudioManager &&) = delete;

  /**
   * Initialize the audio system
   * @param sample_rate Target sample rate
   * @return true on success, false on failure
   */
  bool init(UINT32 sample_rate);

  /**
   * Shutdown and cleanup audio system
   */
  void shutdown();

  /**
   * Start audio streaming
   * @return true on success, false on failure
   */
  bool start();

  /**
   * Stop audio streaming
   */
  void stop();

  /**
   * Set the audio generation callback
   * @param callback Function to call for generating audio samples
   */
  void set_callback(AudioCallback callback);

  /**
   * Clear the audio generation callback
   */
  void clear_callback();

  /**
   * Check if audio system is initialized
   */
  bool is_initialized() const { return initialized; }

  /**
   * Check if audio streaming is active
   */
  bool is_running() const { return running; }

  /**
   * Get current sample rate
   */
  UINT32 get_sample_rate() const { return sample_rate; }

private:
  // Static callback wrapper for C API
  static UINT32 fill_buffer_static(void *drv_struct, void *user_param,
                                   UINT32 buf_size, void *data);

  // Instance callback implementation
  UINT32 fill_buffer(UINT32 buf_size, void *data);

  // Find and select a suitable real-time audio driver
  bool find_suitable_driver();

  // Audio system state
  void *aud_drv;
  UINT32 driver_index;
  std::vector<UINT32> driver_order_;
  UINT32 smpl_size;
  UINT32 smpl_alloc;
  UINT32 sample_rate;

  // Sample buffers
  std::vector<DEV_SMPL> smpl_data[2];

  // User callback
  AudioCallback callback;

  // State flags
  bool initialized;
  bool running;
};
