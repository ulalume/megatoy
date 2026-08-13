#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace audio {

/**
 * Ring buffer of recent stereo output, written by the audio thread and read
 * by the UI.
 *
 * Single producer, single consumer. Reads are not synchronized against
 * concurrent writes: a snapshot taken while the producer wraps around can mix
 * samples from two passes. That is acceptable for a scope display -- the cost
 * of a lock on the audio thread is not -- and the buffer is large enough that
 * it takes an unusually stalled UI frame to happen at all.
 */
class ScopeBuffer {
public:
  /// Must be a power of two.
  static constexpr std::size_t kCapacity = 8192;

  ScopeBuffer();

  void clear();

  /// Audio thread: append `frames` interleaved stereo samples in [-1, 1].
  void write(const float *interleaved, std::size_t frames);

  /**
   * UI thread: copy the newest `frames` samples (oldest first) into `left`
   * and `right`, resizing them to the number of frames actually available.
   */
  void snapshot(std::size_t frames, std::vector<float> &left,
                std::vector<float> &right) const;

  /// Total frames written since construction or the last clear().
  std::uint64_t frames_written() const;

  /// True if a sample reached full scale within the last `window` frames.
  bool clipped_within(std::uint64_t window) const;

  /// True while recent output is strong enough to visibly move the waveform.
  bool signal_within(std::uint64_t window) const;

private:
  // Heap-allocated, not std::array: at kCapacity this pair is 64 KB, and a
  // ScopeBuffer lives inside AudioEngine -> AudioManager -> AppServices,
  // which main() holds by value. That is fine on a desktop's 8 MB stack and
  // exactly overflows Emscripten's 64 KB one. Allocated once in the
  // constructor and never resized, so the audio thread's pointer stays valid.
  std::vector<float> left_;
  std::vector<float> right_;

  std::atomic<std::uint64_t> write_position_;
  std::atomic<std::uint64_t> last_clip_position_;
  std::atomic<bool> has_clipped_;
  std::atomic<std::uint64_t> last_signal_position_;
  std::atomic<bool> has_signal_;
};

} // namespace audio
