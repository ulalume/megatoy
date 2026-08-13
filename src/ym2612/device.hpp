#pragma once

#include "audio/lowpass_filter.hpp"
#include "ym2612/types.hpp"
#include "ym2612/ymfm_chip.hpp"
#include <cstdint>
#include <memory>

namespace ym2612 {

class Channel; // Forward declaration

/**
 * YM2612 device: an ymfm chip running at its native rate, resampled to the
 * host output rate.
 *
 * Rendering produces normalized floats so nothing downstream (audio output,
 * analyzers) has to know about the chip's internal scale.
 */
class Device {
public:
  Device();
  ~Device();

  Device(const Device &) = delete;
  Device &operator=(const Device &) = delete;

  /// Mega Drive YM2612 clock.
  static constexpr uint32_t kClock = 7670454;

  void init(uint32_t sample_rate);
  void stop();
  bool is_initialized() const { return chip_ != nullptr; }

  uint32_t sample_rate() const { return sample_rate_; }
  uint32_t native_sample_rate() const;

  Channel channel(ChannelIndex idx);

  void write(uint8_t reg, uint8_t data, bool port = false);
  void write_settings(const GlobalSettings &settings);

  /**
   * Render `frames` stereo frames into `out` as interleaved L/R floats
   * nominally within [-1, 1]. `out` must hold at least `frames * 2` values.
   */
  void render(uint32_t frames, float *out);

private:
  struct Resampler; // owns the vendored libvgm resampler state + scratch

  uint32_t sample_rate_ = 0;
  audio::LowPassFilter lowpass_;
  std::unique_ptr<YmfmChip> chip_;
  std::unique_ptr<Resampler> resampler_;
};

} // namespace ym2612
