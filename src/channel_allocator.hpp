#pragma once

#include "ym2612/note.hpp"
#include "ym2612/types.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace ym2612 {
class Device;
} // namespace ym2612

/**
 * Decides which of the chip's six channels a note plays on.
 *
 * Only the audio thread touches the allocation state. What the UI needs --
 * which keys to light up, which notes to feed the chord display -- is
 * published separately as plain atomics, so drawing a frame never reads the
 * allocator's containers while they are being modified.
 */
class ChannelAllocator {
public:
  ChannelAllocator();

  struct ChannelClaim {
    ym2612::ChannelIndex channel;
    std::optional<ym2612::Note> replaced_note;
  };

  // --- audio thread only ---

  bool is_note_active(const ym2612::Note &note) const;
  std::optional<ChannelClaim> note_on(const ym2612::Note &note,
                                      uint8_t velocity, bool allow_voice_steal);
  std::optional<uint8_t> active_velocity(ym2612::ChannelIndex channel) const;
  bool note_off(const ym2612::Note &note, ym2612::Device &device);
  void release_all(ym2612::Device &device);

  // --- readable from any thread ---

  /// Notes currently sounding, in channel order.
  std::vector<ym2612::Note> published_notes() const;
  bool published_contains(const ym2612::Note &note) const;
  /// Which voices are busy, for the channel display.
  std::array<bool, 6> published_channels() const;

private:
  void publish();

  std::array<bool, 6> channel_key_on_;
  std::array<std::optional<ym2612::Note>, 6> channel_to_note_;
  std::array<uint8_t, 6> channel_velocity_{};
  std::array<uint64_t, 6> channel_order_{};
  uint64_t allocation_counter_ = 0;
  std::map<ym2612::Note, ym2612::ChannelIndex> note_to_channel_;

  // MIDI note number plus one; zero means the channel is idle.
  std::array<std::atomic<uint16_t>, 6> published_{};
};
