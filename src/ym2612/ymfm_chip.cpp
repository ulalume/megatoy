#include "ym2612/ymfm_chip.hpp"

#include <algorithm>
#include <cassert>
#include <ymfm_opn.h>

namespace ym2612 {

namespace {

// ymfm asks the host for external resources (ADPCM ROM, timers, IRQs). The
// YM2612 patch editor needs none of them, so the default no-op interface is
// enough.
class Interface : public ymfm::ymfm_interface {};

struct ChipState {
  // Operator key-on mask per key-on slot (register 0x28, bits [2:0]).
  uint8_t key_state[8] = {};
  uint8_t port0_address = 0;
};

template <typename Chip> void reset_chip(Chip &chip, ChipState &state) {
  chip.reset();
  std::fill(std::begin(state.key_state), std::end(state.key_state), 0);
  state.port0_address = 0;
}

template <typename Chip>
void write_chip(Chip &chip, ChipState &state, uint8_t offset, uint8_t data) {
  // Register 0x28 is key-on/off. The envelope generator only reacts to a
  // change sampled while the chip is clocked. Retriggers require the falling
  // edge to be clocked before the following rising edge.
  if (offset == 0) {
    state.port0_address = data;
  } else if (offset == 1 && state.port0_address == 0x28) {
    const uint8_t slot = data & 0x07;
    const uint8_t new_ops = data & 0xF0;
    const uint8_t prev_ops = state.key_state[slot];
    const uint8_t rising = new_ops & ~prev_ops;

    if (rising != 0) {
      chip.write(0, 0x28);
      chip.write(1, static_cast<uint8_t>((prev_ops & ~rising) | slot));
      typename Chip::output_data discard;
      chip.generate(&discard, 1);
    }

    state.key_state[slot] = new_ops;
  }

  chip.write(offset, data);
}

template <typename Chip>
void render_chip(Chip &chip, int32_t *left, int32_t *right, uint32_t frames) {
  typename Chip::output_data sample;
  for (uint32_t i = 0; i < frames; ++i) {
    chip.generate(&sample, 1);
    left[i] = sample.data[0];
    right[i] = sample.data[1];
  }
}

} // namespace

struct YmfmChip::Impl {
  explicit Impl(uint32_t clock)
      : clock(clock), ym2612_chip(ym2612_interface),
        ym3438_chip(ym3438_interface) {
    reset_chip(ym2612_chip, ym2612_state);
    reset_chip(ym3438_chip, ym3438_state);
  }

  uint32_t clock;
  ChipType chip_type = ChipType::Ym2612;
  Interface ym2612_interface;
  Interface ym3438_interface;
  ymfm::ym2612 ym2612_chip;
  ymfm::ym3438 ym3438_chip;
  ChipState ym2612_state;
  ChipState ym3438_state;
};

YmfmChip::YmfmChip(uint32_t clock) : impl_(std::make_unique<Impl>(clock)) {}

YmfmChip::~YmfmChip() = default;

uint32_t YmfmChip::native_sample_rate() const {
  const uint32_t ym2612_rate = impl_->ym2612_chip.sample_rate(impl_->clock);
  const uint32_t ym3438_rate = impl_->ym3438_chip.sample_rate(impl_->clock);
  // Both OPN2 variants use the same fixed clock divider, so switching cannot
  // invalidate the resampler's source rate.
  assert(ym2612_rate == ym3438_rate);
  return ym2612_rate;
}

ChipType YmfmChip::chip_type() const { return impl_->chip_type; }

void YmfmChip::set_chip_type(ChipType type) { impl_->chip_type = type; }

void YmfmChip::reset() {
  if (impl_->chip_type == ChipType::Ym2612) {
    reset_chip(impl_->ym2612_chip, impl_->ym2612_state);
  } else {
    reset_chip(impl_->ym3438_chip, impl_->ym3438_state);
  }
}

void YmfmChip::write(uint8_t offset, uint8_t data) {
  if (impl_->chip_type == ChipType::Ym2612) {
    write_chip(impl_->ym2612_chip, impl_->ym2612_state, offset, data);
  } else {
    write_chip(impl_->ym3438_chip, impl_->ym3438_state, offset, data);
  }
}

uint8_t YmfmChip::read(uint8_t offset) {
  if (impl_->chip_type == ChipType::Ym2612) {
    return impl_->ym2612_chip.read(offset);
  }
  return impl_->ym3438_chip.read(offset);
}

void YmfmChip::render(int32_t *left, int32_t *right, uint32_t frames) {
  if (left == nullptr || right == nullptr) {
    return;
  }

  // generate() is non-virtual in ymfm; the concrete branch is required for
  // YM3438's clean-DAC implementation to run.
  if (impl_->chip_type == ChipType::Ym2612) {
    render_chip(impl_->ym2612_chip, left, right, frames);
  } else {
    render_chip(impl_->ym3438_chip, left, right, frames);
  }
}

void YmfmChip::stream_update(void *info, uint32_t frames, int32_t **outputs) {
  if (info == nullptr || outputs == nullptr) {
    return;
  }
  static_cast<YmfmChip *>(info)->render(outputs[0], outputs[1], frames);
}

} // namespace ym2612
