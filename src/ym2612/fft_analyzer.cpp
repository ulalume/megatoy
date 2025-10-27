#include "fft_analyzer.hpp"
#include <cmath>
#include <iostream>
#include <kiss_fft.h>

namespace ym2612 {

FFTAnalyzer::FFTAnalyzer(size_t fftSize)
    : fft_size_(fftSize), window_(fftSize), magnitudes_(fftSize / 2) {
  std::cout << "FFTAnalyzer: Initializing with size " << fftSize << std::endl;

  // Initialize magnitudes with zero
  std::fill(magnitudes_.begin(), magnitudes_.end(), -60.0f);

  // Test kiss_fft allocation
  kiss_fft_cfg test_cfg = kiss_fft_alloc(fft_size_, 0, nullptr, nullptr);
  if (test_cfg == nullptr) {
    std::cerr << "FFTAnalyzer: ERROR - kiss_fft_alloc failed for size "
              << fftSize << std::endl;
    return;
  }
  free(test_cfg);
  std::cout << "FFTAnalyzer: kiss_fft allocation test successful" << std::endl;

  // Hann窓
  for (size_t i = 0; i < fft_size_; ++i) {
    window_[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (fft_size_ - 1)));
  }
}

void FFTAnalyzer::compute(const std::vector<float> &samples) {
  if (samples.empty()) {
    std::cerr << "FFTAnalyzer::compute: ERROR - No samples provided"
              << std::endl;
    return;
  }

  kiss_fft_cfg cfg = kiss_fft_alloc(fft_size_, 0, nullptr, nullptr);
  if (cfg == nullptr) {
    std::cerr << "FFTAnalyzer::compute: ERROR - kiss_fft_alloc failed for size "
              << fft_size_ << std::endl;
    return;
  }

  std::vector<kiss_fft_cpx> in(fft_size_), out(fft_size_);

  // Zero-padding: use available samples up to fft_size_, pad rest with zeros
  size_t samples_to_use = std::min(samples.size(), fft_size_);

  for (size_t i = 0; i < samples_to_use; ++i) {
    in[i].r = samples[i] * window_[i];
    in[i].i = 0;
  }

  // Zero-pad the remaining samples
  for (size_t i = samples_to_use; i < fft_size_; ++i) {
    in[i].r = 0;
    in[i].i = 0;
  }

  kiss_fft(cfg, in.data(), out.data());

  for (size_t i = 0; i < fft_size_ / 2; ++i) {
    float mag = std::sqrt(out[i].r * out[i].r + out[i].i * out[i].i);
    magnitudes_[i] = 20.0f * log10f(mag + 1e-6f); // dBスケール
  }

  free(cfg);
}

} // namespace ym2612
