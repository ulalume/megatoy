#include "ym2612/ymfm_chip.hpp"

#include <algorithm>
#include <ymfm_opn.h>

namespace ym2612 {

namespace {

// ymfm asks the host for external resources (ADPCM ROM, timers, IRQs). The
// YM2612 patch editor needs none of them, so the default no-op interface is
// enough.
class Interface : public ymfm::ymfm_interface {};

} // namespace

struct YmfmChip::Impl {
  explicit Impl(uint32_t clock) : clock(clock), chip(interface) { chip.reset(); }

  uint32_t clock;
  Interface interface;
  ymfm::ym2612 chip;

  // Operator key-on mask per key-on slot (register 0x28, bits [2:0]).
  uint8_t key_state[8] = {};
  uint8_t port0_address = 0;
};

YmfmChip::YmfmChip(uint32_t clock) : impl_(std::make_unique<Impl>(clock)) {}

YmfmChip::~YmfmChip() = default;

uint32_t YmfmChip::native_sample_rate() const {
  return impl_->chip.sample_rate(impl_->clock);
}

void YmfmChip::reset() {
  impl_->chip.reset();
  std::fill(std::begin(impl_->key_state), std::end(impl_->key_state), 0);
  impl_->port0_address = 0;
}

void YmfmChip::write(uint8_t offset, uint8_t data) {
  // Register 0x28 is key-on/off. The envelope generator only reacts to a
  // *change* of key state, and that change is sampled when the chip is
  // clocked. A key-off immediately followed by a key-on -- which is what
  // voice stealing and note retriggering produce -- would therefore collapse
  // into "still on" and the envelope would never restart.
  //
  // Force the 1->0 edge to be observed by clocking one sample with the
  // rising operators keyed off. Operators that are already sounding are left
  // untouched so FM3 special mode keeps working.
  if (offset == 0) {
    impl_->port0_address = data;
  } else if (offset == 1 && impl_->port0_address == 0x28) {
    const uint8_t slot = data & 0x07;
    const uint8_t new_ops = data & 0xF0;
    const uint8_t prev_ops = impl_->key_state[slot];
    const uint8_t rising = new_ops & ~prev_ops;

    if (rising != 0) {
      impl_->chip.write(0, 0x28);
      impl_->chip.write(1, static_cast<uint8_t>((prev_ops & ~rising) | slot));
      ymfm::ym2612::output_data discard;
      impl_->chip.generate(&discard, 1);
    }

    impl_->key_state[slot] = new_ops;
  }

  impl_->chip.write(offset, data);
}

void YmfmChip::render(int32_t *left, int32_t *right, uint32_t frames) {
  if (left == nullptr || right == nullptr) {
    return;
  }

  ymfm::ym2612::output_data sample;
  for (uint32_t i = 0; i < frames; ++i) {
    impl_->chip.generate(&sample, 1);
    left[i] = sample.data[0];
    right[i] = sample.data[1];
  }
}

void YmfmChip::stream_update(void *info, uint32_t frames, int32_t **outputs) {
  if (info == nullptr || outputs == nullptr) {
    return;
  }
  static_cast<YmfmChip *>(info)->render(outputs[0], outputs[1], frames);
}

} // namespace ym2612
