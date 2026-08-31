#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace audio {

/**
 * Fraction of one block's deadline that rendering it consumed.
 *
 * `elapsed_ns` is how long the render call took; `frames` at `sample_rate`
 * is how much audio it produced. 1.0 means rendering a block took exactly as
 * long as that block will play for, which is the point where the device runs
 * out of buffered audio and drops out.
 */
constexpr float load_ratio(std::int64_t elapsed_ns, std::uint32_t frames,
                           std::uint32_t sample_rate) {
  if (elapsed_ns <= 0 || frames == 0 || sample_rate == 0) {
    return 0.0f;
  }
  const double deadline_ns = 1'000'000'000.0 * static_cast<double>(frames) /
                             static_cast<double>(sample_rate);
  return static_cast<float>(static_cast<double>(elapsed_ns) / deadline_ns);
}

/**
 * The recent render load, written by the audio thread and read by the UI.
 *
 * record() does a division and two stores into a fixed array. It allocates
 * nothing, takes no lock, and never blocks; the static_asserts below hold the
 * lock-free part to the compiler. A reader that the writer laps mid-read gets
 * one stale value in a graph of many, which is the price of never making the
 * audio thread wait for it.
 */
class LoadMeter {
public:
  static constexpr std::size_t kCapacity = 64;
  static_assert((kCapacity & (kCapacity - 1)) == 0,
                "the write counter wraps, so the ring has to divide 2^32");

  struct History {
    std::array<float, kCapacity> values{};
    /// Entries in `values`, oldest first.
    std::size_t count = 0;
  };

  /// Audio thread only.
  void record(std::int64_t elapsed_ns, std::uint32_t frames,
              std::uint32_t sample_rate) {
    const std::uint32_t written = written_.load(std::memory_order_relaxed);
    values_[written % kCapacity].store(
        load_ratio(elapsed_ns, frames, sample_rate), std::memory_order_relaxed);
    written_.store(written + 1, std::memory_order_release);
  }

  /// Drop the history. Only safe while nothing is rendering.
  void clear() {
    for (auto &value : values_) {
      value.store(0.0f, std::memory_order_relaxed);
    }
    written_.store(0, std::memory_order_release);
  }

  /// The most recent values, oldest first. Safe to call from any thread.
  History history() const {
    const std::uint32_t written = written_.load(std::memory_order_acquire);
    History out;
    out.count = static_cast<std::size_t>(std::min<std::uint32_t>(
        written, static_cast<std::uint32_t>(kCapacity)));
    const std::uint32_t first = written - static_cast<std::uint32_t>(out.count);
    for (std::size_t i = 0; i < out.count; ++i) {
      out.values[i] =
          values_[(first + i) % kCapacity].load(std::memory_order_relaxed);
    }
    return out;
  }

private:
  static_assert(std::atomic<float>::is_always_lock_free);
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

  std::array<std::atomic<float>, kCapacity> values_{};
  std::atomic<std::uint32_t> written_{0};
};

} // namespace audio
