#pragma once

#include "audio/scope_buffer.hpp"
#include "ym2612/device.hpp"
#include "ym2612/patch.hpp"
#include <cstdint>
#include <vector>

class AudioEngine {
public:
  AudioEngine();
  ~AudioEngine() = default;

  AudioEngine(const AudioEngine &) = delete;
  AudioEngine &operator=(const AudioEngine &) = delete;
  AudioEngine(AudioEngine &&) = delete;
  AudioEngine &operator=(AudioEngine &&) = delete;

  bool initialize(uint32_t sample_rate);
  void shutdown();

  /// Fill `data` with up to `buf_size` bytes of interleaved stereo s16 audio.
  /// Returns the number of bytes written.
  uint32_t render(uint32_t buf_size, void *data);

  void apply_patch_to_all_channels(const ym2612::Patch &patch);

  ym2612::Device &device() { return device_; }
  const ym2612::Device &device() const { return device_; }

  audio::ScopeBuffer &scope_buffer() { return scope_buffer_; }
  const audio::ScopeBuffer &scope_buffer() const { return scope_buffer_; }

  uint32_t sample_rate() const { return sample_rate_; }
  uint32_t frame_size() const { return frame_size_; }
  bool is_running() const { return running_; }

private:
  uint32_t sample_rate_;
  uint32_t frame_size_;

  std::vector<float> mix_buffer_; // interleaved stereo, [-1, 1]
  ym2612::Device device_;
  audio::ScopeBuffer scope_buffer_;
  bool running_;
};
