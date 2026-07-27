#include "audio/spectrum_analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <kiss_fftr.h>

namespace audio {

namespace {

std::size_t round_up_to_power_of_two(std::size_t value) {
  std::size_t size = 64;
  while (size < value) {
    size *= 2;
  }
  return size;
}

} // namespace

struct SpectrumAnalyzer::Plan {
  kiss_fftr_cfg cfg = nullptr;
  std::vector<kiss_fft_cpx> output;

  ~Plan() { kiss_fftr_free(cfg); }
};

SpectrumAnalyzer::SpectrumAnalyzer(std::size_t fft_size)
    : fft_size_(round_up_to_power_of_two(fft_size)), plan_(new Plan()) {
  window_.resize(fft_size_);
  input_.assign(fft_size_, 0.0f);
  magnitudes_db_.assign(fft_size_ / 2 + 1, kFloorDb);

  // Periodic Hann window: matches the FFT's assumption that the frame repeats.
  double window_sum = 0.0;
  for (std::size_t i = 0; i < fft_size_; ++i) {
    window_[i] = static_cast<float>(
        0.5 * (1.0 - std::cos(2.0 * M_PI * static_cast<double>(i) /
                              static_cast<double>(fft_size_))));
    window_sum += window_[i];
  }

  // A full-scale sine puts half its energy in each of two mirrored bins, so
  // its peak bin magnitude is amplitude * sum(window) / 2. Dividing by that
  // makes such a sine read exactly 0 dBFS.
  scale_ = window_sum > 0.0 ? static_cast<float>(2.0 / window_sum) : 1.0f;

  plan_->cfg = kiss_fftr_alloc(static_cast<int>(fft_size_), 0, nullptr,
                               nullptr);
  plan_->output.resize(fft_size_ / 2 + 1);
}

SpectrumAnalyzer::~SpectrumAnalyzer() { delete plan_; }

void SpectrumAnalyzer::reset() {
  std::fill(magnitudes_db_.begin(), magnitudes_db_.end(), kFloorDb);
}

float SpectrumAnalyzer::bin_frequency(std::size_t bin,
                                      uint32_t sample_rate) const {
  return static_cast<float>(bin) * static_cast<float>(sample_rate) /
         static_cast<float>(fft_size_);
}

void SpectrumAnalyzer::analyze(const float *samples, std::size_t count,
                               float smoothing) {
  if (plan_->cfg == nullptr) {
    return;
  }
  if (samples == nullptr || count == 0) {
    reset();
    return;
  }

  // Use the newest fft_size samples; zero-pad at the front if there are
  // fewer, so the window still lines up with the end of the signal.
  const std::size_t used = std::min(count, fft_size_);
  const std::size_t pad = fft_size_ - used;
  const float *tail = samples + (count - used);

  // Remove DC before windowing. The YM2612's DAC is discontinuous around
  // zero, so its output carries a constant offset that would otherwise
  // dominate the lowest bins.
  double sum = 0.0;
  for (std::size_t i = 0; i < used; ++i) {
    sum += tail[i];
  }
  const float mean = static_cast<float>(sum / static_cast<double>(used));

  std::fill_n(input_.begin(), pad, 0.0f);
  for (std::size_t i = 0; i < used; ++i) {
    input_[pad + i] = (tail[i] - mean) * window_[pad + i];
  }

  kiss_fftr(plan_->cfg, input_.data(), plan_->output.data());

  for (std::size_t bin = 0; bin < magnitudes_db_.size(); ++bin) {
    const kiss_fft_cpx &value = plan_->output[bin];
    const float magnitude =
        std::sqrt(value.r * value.r + value.i * value.i) * scale_;
    float db = 20.0f * std::log10(std::max(magnitude, 1e-9f));
    db = std::max(db, kFloorDb);

    // Attack immediately, release slowly: peaks stay readable without the
    // display smearing upward.
    const float previous = magnitudes_db_[bin];
    magnitudes_db_[bin] =
        (db > previous) ? db : previous + (db - previous) * (1.0f - smoothing);
  }
}

} // namespace audio
