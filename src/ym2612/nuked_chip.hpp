#pragma once

#include "ym2612/chip.hpp"
#include <cstdint>
#include <memory>

namespace ym2612 {

/**
 * Nuked-OPN2.
 *
 * The core advances one internal clock at a time and latches bus writes
 * through a pipeline that needs clocking, so the chip instance and the queue
 * of writes waiting for their turn live behind a pimpl.
 */
class NukedChip final : public Chip {
public:
  explicit NukedChip(uint32_t clock);
  ~NukedChip() override;

  NukedChip(const NukedChip &) = delete;
  NukedChip &operator=(const NukedChip &) = delete;

  uint32_t native_sample_rate() const override;

  ChipType chip_type() const override;
  void set_chip_type(ChipType type) override;

  void reset() override;

  void write(uint8_t offset, uint8_t data) override;

  uint8_t read(uint8_t offset) override;

  void render(int32_t *left, int32_t *right, uint32_t frames) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ym2612
