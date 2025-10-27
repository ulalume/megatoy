#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ym2612 {

class WaveSampler {
public:
  WaveSampler();

  void clear();

  void push_samples(const int32_t *left, const int32_t *right,
                    std::size_t sample_count);

  void latest_samples(std::size_t sample_count, std::vector<float> &samples,
                      bool is_left) const;

  static constexpr std::size_t buffer_size() { return kBufferSize; }

  bool is_volume_warning() const;

private:
  static constexpr std::size_t kBufferSize = 2048;
  static constexpr float kNormalization = 1.0f / 32768.0f;

  using Buffer = std::array<float, kBufferSize>;

  Buffer left_buffer_;
  Buffer right_buffer_;

  mutable std::atomic<uint32_t> write_index_;
  mutable std::atomic<uint32_t> valid_count_;

  mutable std::atomic<bool> volume_warning_;
};

} // namespace ym2612
