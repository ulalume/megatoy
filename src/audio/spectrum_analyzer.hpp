#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace audio {

/**
 * Windowed real FFT producing a magnitude spectrum in dBFS.
 *
 * The FFT plan and all scratch buffers are allocated once, so analyze() does
 * no allocation. 0 dBFS corresponds to a full-scale sine wave, which makes
 * the vertical axis meaningful rather than decorative.
 */
class SpectrumAnalyzer {
public:
  /// `fft_size` must be even; it is rounded up to the next power of two.
  explicit SpectrumAnalyzer(std::size_t fft_size = 2048);
  ~SpectrumAnalyzer();

  SpectrumAnalyzer(const SpectrumAnalyzer &) = delete;
  SpectrumAnalyzer &operator=(const SpectrumAnalyzer &) = delete;

  /// Quietest level reported; bins below this are clamped.
  static constexpr float kFloorDb = -100.0f;

  std::size_t fft_size() const { return fft_size_; }
  /// Number of magnitude bins, i.e. fft_size / 2 + 1.
  std::size_t bin_count() const { return magnitudes_db_.size(); }

  float bin_frequency(std::size_t bin, uint32_t sample_rate) const;

  /**
   * Analyze the newest `count` samples of `samples` (the last fft_size are
   * used; shorter input is zero-padded).
   *
   * `smoothing` in [0, 1) blends with the previous result to steady the
   * display: 0 disables it, higher values decay more slowly. Rising levels
   * are always taken immediately so transients are not blunted.
   */
  void analyze(const float *samples, std::size_t count, float smoothing);

  void reset();

  const std::vector<float> &magnitudes_db() const { return magnitudes_db_; }

private:
  struct Plan;

  std::size_t fft_size_;
  float scale_; // converts FFT magnitude to a full-scale-sine ratio
  std::vector<float> window_;
  std::vector<float> magnitudes_db_;
  std::vector<float> input_;
  Plan *plan_;
};

} // namespace audio
