#include "audio/scope_buffer.hpp"

#include <algorithm>
#include <cmath>

namespace audio {

namespace {
constexpr std::size_t kMask = ScopeBuffer::kCapacity - 1;
static_assert((ScopeBuffer::kCapacity & kMask) == 0,
              "ScopeBuffer::kCapacity must be a power of two");
} // namespace

ScopeBuffer::ScopeBuffer()
    : left_(kCapacity, 0.0f), right_(kCapacity, 0.0f), write_position_(0),
      last_clip_position_(0), has_clipped_(false) {}

void ScopeBuffer::clear() {
  std::fill(left_.begin(), left_.end(), 0.0f);
  std::fill(right_.begin(), right_.end(), 0.0f);
  write_position_.store(0, std::memory_order_relaxed);
  last_clip_position_.store(0, std::memory_order_relaxed);
  has_clipped_.store(false, std::memory_order_relaxed);
}

void ScopeBuffer::write(const float *interleaved, std::size_t frames) {
  if (interleaved == nullptr || frames == 0) {
    return;
  }

  std::uint64_t position = write_position_.load(std::memory_order_relaxed);
  bool clipped = false;

  for (std::size_t i = 0; i < frames; ++i) {
    const float l = interleaved[i * 2 + 0];
    const float r = interleaved[i * 2 + 1];

    if (std::fabs(l) >= 1.0f || std::fabs(r) >= 1.0f) {
      clipped = true;
      last_clip_position_.store(position + i, std::memory_order_relaxed);
    }

    const std::size_t index = static_cast<std::size_t>(position + i) & kMask;
    left_[index] = std::clamp(l, -1.0f, 1.0f);
    right_[index] = std::clamp(r, -1.0f, 1.0f);
  }

  if (clipped) {
    has_clipped_.store(true, std::memory_order_relaxed);
  }
  write_position_.store(position + frames, std::memory_order_release);
}

void ScopeBuffer::snapshot(std::size_t frames, std::vector<float> &left,
                           std::vector<float> &right) const {
  const std::uint64_t end = write_position_.load(std::memory_order_acquire);
  const std::size_t available =
      static_cast<std::size_t>(std::min<std::uint64_t>(end, kCapacity));
  const std::size_t count = std::min(frames, available);

  left.resize(count);
  right.resize(count);
  if (count == 0) {
    return;
  }

  const std::uint64_t start = end - count;
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t index = static_cast<std::size_t>(start + i) & kMask;
    left[i] = left_[index];
    right[i] = right_[index];
  }
}

std::uint64_t ScopeBuffer::frames_written() const {
  return write_position_.load(std::memory_order_acquire);
}

bool ScopeBuffer::clipped_within(std::uint64_t window) const {
  if (!has_clipped_.load(std::memory_order_relaxed)) {
    return false;
  }
  const std::uint64_t now = write_position_.load(std::memory_order_acquire);
  const std::uint64_t last =
      last_clip_position_.load(std::memory_order_relaxed);
  return now - last <= window;
}

} // namespace audio
