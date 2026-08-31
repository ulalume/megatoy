#pragma once

#include <cstdint>

namespace ym2612 {

enum class ChipType : uint8_t { Ym2612, Ym3438 };

/// The emulation core a chip is rendered with.
enum class CoreType : uint8_t { Nuked, Ymfm };

/**
 * An OPN2 chip behind the bus its host writes to.
 *
 * The chip always runs at its native rate (clock / 144); converting to the
 * output rate is the caller's job. Every implementation renders on the same
 * nominal scale (kFullScale), so which one is in place changes nothing
 * downstream.
 */
class Chip {
public:
  virtual ~Chip() = default;

  /// Full-scale magnitude of a rendered sample.
  static constexpr int32_t kFullScale = 32768;

  /// Native chip sample rate in Hz (clock / 144 for the YM2612).
  virtual uint32_t native_sample_rate() const = 0;

  virtual ChipType chip_type() const = 0;
  virtual void set_chip_type(ChipType type) = 0;

  virtual void reset() = 0;

  /**
   * Write to the chip's bus.
   * @param offset 0/1 = port 0 address/data, 2/3 = port 1 address/data.
   */
  virtual void write(uint8_t offset, uint8_t data) = 0;

  /// Read from the chip's bus.
  virtual uint8_t read(uint8_t offset) = 0;

  /// Render `frames` samples into two separate 32-bit channel buffers.
  virtual void render(int32_t *left, int32_t *right, uint32_t frames) = 0;

  /**
   * Adapter matching libvgm's DEVFUNC_UPDATE, for use with the resampler.
   * `info` must be a Chip pointer, not a pointer to a derived type.
   */
  static void stream_update(void *info, uint32_t frames, int32_t **outputs);
};

inline void Chip::stream_update(void *info, uint32_t frames,
                                int32_t **outputs) {
  if (info == nullptr || outputs == nullptr) {
    return;
  }
  static_cast<Chip *>(info)->render(outputs[0], outputs[1], frames);
}

} // namespace ym2612
