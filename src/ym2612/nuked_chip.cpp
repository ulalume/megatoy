#include "ym2612/nuked_chip.hpp"

#include <array>
#include <cstddef>

extern "C" {
#include <ym3438.h>
}

namespace ym2612 {

namespace {

/// Internal clocks per output sample; each is six master clocks.
constexpr uint32_t kClocksPerSample = 24;
constexpr uint32_t kMasterClocksPerClock = 6;

/// Operator slots the core runs, one per internal clock of a sample.
constexpr std::size_t kSlots = 24;

/// Envelope level at full attenuation, where a slot contributes nothing.
constexpr uint16_t kEnvelopeFloor = 0x3ff;

/**
 * The six channels are multiplexed across the 24 clocks of a sample, three
 * clocks each, and a channel is nine bits. Accumulating the clocks therefore
 * peaks at three times a parallel six-channel mix.
 */
constexpr int32_t kAccumulatedFullScale = 3 * 6 * 256;

/**
 * Internal clocks between one bus write and the next.
 *
 * A write is latched by a pipeline that only advances while the chip is
 * clocked: two writes issued closer together than this overwrite each other
 * before the first is seen. An address/data pair spends one spacing between
 * its halves, so a key-off followed by a key-on leaves two of them -- more
 * than the 24 clocks a key-on latch takes to come round.
 */
constexpr uint32_t kWriteSpacingClocks = 15;

/// Room for every write a full patch update issues, several times over.
constexpr std::size_t kWriteQueueSize = 2048;

/**
 * Frames of unchanging output required before the core stops being clocked.
 *
 * A bus write reaches the envelope state through latches that only advance
 * while the chip is clocked, so the count restarts at every write and has to
 * outlast those latches before the envelopes read as settled.
 */
constexpr uint32_t kSettledFramesBeforeRest = 4;

Bit32u nuked_mode(ChipType type) {
  // ym3438_mode_ym2612 selects the nine-bit DAC with the ladder effect;
  // ym3438_mode_readmode selects the CMOS part, which answers a status read
  // on either port.
  return type == ChipType::Ym2612 ? ym3438_mode_ym2612 : ym3438_mode_readmode;
}

} // namespace

struct NukedChip::Impl {
  struct PendingWrite {
    uint8_t offset = 0;
    uint8_t data = 0;
  };

  explicit Impl(uint32_t clock) : clock_hz(clock) { OPN2_Reset(&chip); }

  /**
   * OPN2_SetChipType writes a variable shared by the whole core rather than
   * per-instance state, so the mode is republished before anything reads it.
   */
  void select_mode() { OPN2_SetChipType(nuked_mode(chip_type)); }

  void clear_pending() {
    head = 0;
    tail = 0;
    pending = 0;
    clocks_until_write = 0;
    settled_frames = 0;
  }

  void release_write() {
    const PendingWrite &entry = queue[head];
    OPN2_Write(&chip, entry.offset, entry.data);
    head = (head + 1) % kWriteQueueSize;
    --pending;
    settled_frames = 0;
  }

  void queue_write(uint8_t offset, uint8_t data) {
    settled_frames = 0;
    if (pending == kWriteQueueSize) {
      // A whole queue of writes has arrived without the chip being clocked.
      // Let the oldest through and clock far enough for it to be latched; the
      // samples that produces belong to no render call and are dropped.
      release_write();
      Bit16s discard[2];
      for (uint32_t i = 0; i < kWriteSpacingClocks; ++i) {
        OPN2_Clock(&chip, discard);
      }
      clocks_until_write = 0;
    }
    queue[tail] = PendingWrite{offset, data};
    tail = (tail + 1) % kWriteQueueSize;
    ++pending;
  }

  /// Advance one internal clock, releasing a queued write when one is due.
  void advance(int32_t &left, int32_t &right) {
    if (clocks_until_write > 0) {
      --clocks_until_write;
    } else if (pending > 0) {
      release_write();
      clocks_until_write = kWriteSpacingClocks;
    }

    Bit16s out[2];
    OPN2_Clock(&chip, out);
    left += out[0];
    right += out[1];
  }

  /**
   * Every envelope at full attenuation with no key held. Nothing inside the
   * core can lift an envelope out of that state, so its output holds until
   * the next bus write.
   */
  bool envelopes_at_rest() const {
    for (std::size_t slot = 0; slot < kSlots; ++slot) {
      if (chip.eg_level[slot] != kEnvelopeFloor || chip.eg_kon[slot] != 0) {
        return false;
      }
    }
    return true;
  }

  bool at_rest() const { return settled_frames >= kSettledFramesBeforeRest; }

  /// Record what a clocked frame produced and whether the core is at rest.
  void observe_frame(int32_t left, int32_t right) {
    if (pending > 0 || !envelopes_at_rest()) {
      settled_frames = 0;
      return;
    }
    if (settled_frames > 0 && left == rest_left && right == rest_right) {
      ++settled_frames;
      return;
    }
    rest_left = left;
    rest_right = right;
    settled_frames = 1;
  }

  /// Anything that can change the core puts it back on the clocked path.
  void disturb() { settled_frames = 0; }

  uint32_t clock_hz;
  ChipType chip_type = ChipType::Ym2612;
  ym3438_t chip{};
  std::array<PendingWrite, kWriteQueueSize> queue{};
  std::size_t head = 0;
  std::size_t tail = 0;
  std::size_t pending = 0;
  uint32_t clocks_until_write = 0;
  uint32_t settled_frames = 0;
  int32_t rest_left = 0;
  int32_t rest_right = 0;
};

NukedChip::NukedChip(uint32_t clock) : impl_(std::make_unique<Impl>(clock)) {}

NukedChip::~NukedChip() = default;

uint32_t NukedChip::native_sample_rate() const {
  return impl_->clock_hz / (kClocksPerSample * kMasterClocksPerClock);
}

ChipType NukedChip::chip_type() const { return impl_->chip_type; }

void NukedChip::set_chip_type(ChipType type) {
  impl_->chip_type = type;
  impl_->select_mode();
  impl_->disturb();
}

void NukedChip::reset() {
  OPN2_Reset(&impl_->chip);
  impl_->clear_pending();
}

void NukedChip::write(uint8_t offset, uint8_t data) {
  impl_->select_mode();
  impl_->queue_write(offset, data);
}

uint8_t NukedChip::read(uint8_t offset) {
  impl_->select_mode();
  impl_->disturb();
  return OPN2_Read(&impl_->chip, offset);
}

void NukedChip::render(int32_t *left, int32_t *right, uint32_t frames) {
  if (left == nullptr || right == nullptr) {
    return;
  }
  impl_->select_mode();

  for (uint32_t frame = 0; frame < frames; ++frame) {
    int32_t accumulated_left = 0;
    int32_t accumulated_right = 0;
    if (impl_->at_rest()) {
      // A chip with nothing to play does not emit zero: the ladder in the
      // YM2612's DAC leaves a constant offset on every clock. Replaying the
      // frame the core produced while it was last clocked carries that offset
      // through the pause, where emitting silence would step the signal at
      // both ends of it.
      accumulated_left = impl_->rest_left;
      accumulated_right = impl_->rest_right;
    } else {
      for (uint32_t tick = 0; tick < kClocksPerSample; ++tick) {
        impl_->advance(accumulated_left, accumulated_right);
      }
      impl_->observe_frame(accumulated_left, accumulated_right);
    }
    left[frame] = accumulated_left * kFullScale / kAccumulatedFullScale;
    right[frame] = accumulated_right * kFullScale / kAccumulatedFullScale;
  }
}

} // namespace ym2612
