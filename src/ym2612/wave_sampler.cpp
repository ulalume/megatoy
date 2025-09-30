#include "wave_sampler.hpp"

#include <algorithm>

namespace ym2612 {

WaveSampler::WaveSampler() : write_index_(0), valid_count_(0) { clear(); }

void WaveSampler::clear() {
  left_buffer_.fill(0.0f);
  right_buffer_.fill(0.0f);
  write_index_.store(0, std::memory_order_relaxed);
  valid_count_.store(0, std::memory_order_relaxed);
}

void WaveSampler::push_samples(const int32_t *left, const int32_t *right,
                               std::size_t sample_count) {
  if (!left || !right || sample_count == 0) {
    return;
  }

  uint32_t index = write_index_.load(std::memory_order_relaxed);
  uint32_t valid = valid_count_.load(std::memory_order_relaxed);

  for (std::size_t i = 0; i < sample_count; ++i) {
    left_buffer_[index] = std::clamp(left[i] * kNormalization, -1.0f, 1.0f);
    right_buffer_[index] = std::clamp(right[i] * kNormalization, -1.0f, 1.0f);

    index = (index + 1) % kBufferSize;
    if (valid < kBufferSize) {
      ++valid;
    }
  }

  write_index_.store(index, std::memory_order_release);
  valid_count_.store(valid, std::memory_order_release);
}

void WaveSampler::latest_samples(std::size_t sample_count,
                                 std::vector<float> &left,
                                 std::vector<float> &right) const {
  const uint32_t valid = valid_count_.load(std::memory_order_acquire);
  if (valid == 0) {
    left.clear();
    right.clear();
    return;
  }

  const std::size_t count = std::min<std::size_t>(sample_count, valid);
  left.resize(count);
  right.resize(count);

  const uint32_t write_index = write_index_.load(std::memory_order_acquire);
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t offset =
        (write_index + kBufferSize - count + i) % kBufferSize;
    left[i] = left_buffer_[offset];
    right[i] = right_buffer_[offset];
  }
}

} // namespace ym2612
