#pragma once

#include <cstdint>
#include <memory>

namespace ym2612 {

enum class ChipType : uint8_t { Ym2612, Ym3438 };

/**
 * Thin wrapper around ymfm's YM2612-family cores.
 *
 * ymfm's headers are template-heavy, so the chip instance lives behind a
 * pimpl and is compiled in exactly one translation unit.
 *
 * The chip always runs at its native rate (clock / 144); converting to the
 * output rate is the caller's job.
 */
class YmfmChip {
public:
  explicit YmfmChip(uint32_t clock);
  ~YmfmChip();

  YmfmChip(const YmfmChip &) = delete;
  YmfmChip &operator=(const YmfmChip &) = delete;

  /// Native chip sample rate in Hz (clock / 144 for the YM2612).
  uint32_t native_sample_rate() const;

  ChipType chip_type() const;
  void set_chip_type(ChipType type);

  void reset();

  /**
   * Write to the chip's bus.
   * @param offset 0/1 = port 0 address/data, 2/3 = port 1 address/data.
   */
  void write(uint8_t offset, uint8_t data);

  /// Read from the chip's bus.
  uint8_t read(uint8_t offset);

  /// Render `frames` samples into two separate 32-bit channel buffers.
  void render(int32_t *left, int32_t *right, uint32_t frames);

  /// Adapter matching libvgm's DEVFUNC_UPDATE, for use with the resampler.
  static void stream_update(void *info, uint32_t frames, int32_t **outputs);

  /// Full-scale magnitude of a rendered sample.
  static constexpr int32_t kFullScale = 32768;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ym2612
