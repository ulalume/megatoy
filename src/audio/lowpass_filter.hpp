#pragma once

#include <cstdint>

namespace audio {

/**
 * First-order RC low-pass used by ctrmml-cmd and BlastEm's analog output
 * stage emulation. Keeping the 16.16 fixed-point calculation here makes the
 * preview follow ctrmml-cmd's 3390 Hz response sample for sample.
 */
class LowPassFilter {
public:
  void init(std::uint32_t sample_rate, double cutoff_hz = 3390.0) {
    constexpr double kPi = 3.14159265358979323846;
    if (sample_rate == 0 || cutoff_hz <= 0.0) {
      alpha_ = 0;
    } else {
      const double rc = 1.0 / (2.0 * kPi * cutoff_hz);
      const double dt = 1.0 / static_cast<double>(sample_rate);
      alpha_ = static_cast<std::int32_t>(65536.0 * dt / (dt + rc));
    }
    last_left_ = 0;
    last_right_ = 0;
  }

  void apply(std::int32_t &left, std::int32_t &right) {
    const std::int64_t filtered_left =
        static_cast<std::int64_t>(left) * alpha_ +
        static_cast<std::int64_t>(last_left_) * (65536 - alpha_);
    const std::int64_t filtered_right =
        static_cast<std::int64_t>(right) * alpha_ +
        static_cast<std::int64_t>(last_right_) * (65536 - alpha_);
    last_left_ = static_cast<std::int32_t>(filtered_left >> 16);
    last_right_ = static_cast<std::int32_t>(filtered_right >> 16);
    left = last_left_;
    right = last_right_;
  }

private:
  std::int32_t last_left_ = 0;
  std::int32_t last_right_ = 0;
  std::int32_t alpha_ = 0;
};

} // namespace audio
