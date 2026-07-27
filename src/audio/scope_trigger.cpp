#include "audio/scope_trigger.hpp"

#include <algorithm>
#include <cmath>

namespace audio {

namespace {

// Below this the signal is treated as silence and left unaligned; triggering
// on noise looks worse than not triggering at all.
constexpr float kNoiseFloor = 1.0f / 4096.0f;

} // namespace

std::size_t find_trigger(const float *samples, std::size_t count,
                         std::size_t window_size) {
  if (samples == nullptr || count <= window_size) {
    return 0;
  }

  const std::size_t latest_start = count - window_size;

  // Work against the mean so a DC offset -- which the YM2612's discontinuous
  // DAC always produces -- does not push every sample to one side of zero.
  double sum = 0.0;
  for (std::size_t i = 0; i < count; ++i) {
    sum += samples[i];
  }
  const float mean = static_cast<float>(sum / static_cast<double>(count));

  float amplitude = 0.0f;
  for (std::size_t i = 0; i < count; ++i) {
    amplitude = std::max(amplitude, std::fabs(samples[i] - mean));
  }
  if (amplitude < kNoiseFloor) {
    return latest_start;
  }

  // Require the signal to fall below -hysteresis before an upward crossing
  // counts, so ripple around zero does not trigger repeatedly.
  const float hysteresis = amplitude * 0.1f;

  // Scan forward, keeping the last qualifying crossing: the trace then shows
  // the most recent audio while still starting at a consistent phase. Any two
  // candidates are a whole number of periods apart, so which one wins does not
  // change what the window looks like.
  bool armed = false;
  bool found = false;
  std::size_t trigger = latest_start;

  for (std::size_t i = 1; i <= latest_start; ++i) {
    const float previous = samples[i - 1] - mean;
    const float current = samples[i] - mean;

    if (current < -hysteresis) {
      armed = true;
    } else if (armed && previous < 0.0f && current >= 0.0f) {
      trigger = i;
      found = true;
      armed = false;
    }
  }

  return found ? trigger : latest_start;
}

} // namespace audio
