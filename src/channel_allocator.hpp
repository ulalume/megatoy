#pragma once

#include "ym2612/note.hpp"
#include "ym2612/types.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <optional>

namespace ym2612 {
class Device;
} // namespace ym2612

class ChannelAllocator {
public:
  ChannelAllocator();

  struct ChannelClaim {
    ym2612::ChannelIndex channel;
    std::optional<ym2612::Note> replaced_note;
  };

  bool is_note_active(const ym2612::Note &note) const;
  std::optional<ChannelClaim> note_on(const ym2612::Note &note,
                                      bool allow_voice_steal);
  bool note_off(const ym2612::Note &note, ym2612::Device &device);
  void release_all(ym2612::Device &device);

  const std::array<bool, 6> &channel_usage() const { return channel_key_on_; }
  const std::map<ym2612::Note, ym2612::ChannelIndex> &active_notes() const {
    return note_to_channel_;
  }

private:
  std::array<bool, 6> channel_key_on_;
  std::array<std::optional<ym2612::Note>, 6> channel_to_note_;
  std::array<uint64_t, 6> channel_order_{};
  uint64_t allocation_counter_ = 0;
  std::map<ym2612::Note, ym2612::ChannelIndex> note_to_channel_;
};
