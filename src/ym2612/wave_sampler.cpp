#include "wave_sampler.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace ym2612 {

WaveSampler::WaveSampler() : write_index_(0), valid_count_(0) { clear(); }

void WaveSampler::clear() {
  left_buffer_.fill(0.0f);
  right_buffer_.fill(0.0f);
  write_index_.store(0, std::memory_order_relaxed);
  valid_count_.store(0, std::memory_order_relaxed);
  volume_warning_.store(false, std::memory_order_relaxed);
}

void WaveSampler::push_frames(const float *interleaved,
                              std::size_t frame_count) {
  if (!interleaved || frame_count == 0) {
    return;
  }

  uint32_t index = write_index_.load(std::memory_order_relaxed);
  uint32_t valid = valid_count_.load(std::memory_order_relaxed);
  bool found_loud_sample = false;

  for (std::size_t i = 0; i < frame_count; ++i) {
    const float left_sample = interleaved[i * 2 + 0];
    const float right_sample = interleaved[i * 2 + 1];
    found_loud_sample =
        found_loud_sample ||
        std::max(std::abs(left_sample), std::abs(right_sample)) >= 1.0f;

    left_buffer_[index] = std::clamp(left_sample, -1.0f, 1.0f);
    right_buffer_[index] = std::clamp(right_sample, -1.0f, 1.0f);

    index = (index + 1) % kBufferSize;
    if (valid < kBufferSize) {
      ++valid;
    }
  }

  write_index_.store(index, std::memory_order_release);
  valid_count_.store(valid, std::memory_order_release);

  volume_warning_.store(found_loud_sample, std::memory_order_relaxed);
}

void WaveSampler::latest_samples(std::size_t sample_count,
                                 std::vector<float> &samples,
                                 bool is_left) const {
  const uint32_t valid = valid_count_.load(std::memory_order_acquire);
  if (valid == 0) {
    samples.clear();
    return;
  }

  const std::size_t count = std::min<std::size_t>(sample_count, valid);
  samples.resize(count);

  const uint32_t write_index = write_index_.load(std::memory_order_acquire);
  auto &buffer = is_left ? left_buffer_ : right_buffer_;
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t offset =
        (write_index + kBufferSize - count + i) % kBufferSize;
    samples[i] = buffer[offset];
  }
}
bool WaveSampler::is_volume_warning() const {
  return volume_warning_.load(std::memory_order_relaxed);
}

} // namespace ym2612
