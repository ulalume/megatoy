#pragma once
#include <complex>
#include <vector>

namespace ym2612 {

class FFTAnalyzer {
public:
  FFTAnalyzer(size_t fft_size);
  void compute(const std::vector<float> &samples);
  const std::vector<float> &magnitudes() const { return magnitudes_; }

private:
  size_t fft_size_;
  std::vector<float> window_;
  std::vector<float> magnitudes_;
};

} // namespace ym2612
