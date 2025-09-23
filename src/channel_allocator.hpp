#pragma once

#include "ym2612/types.hpp"
#include "ym2612/note.hpp"
#include <array>
#include <map>

namespace ym2612 {
class Device;
} // namespace ym2612

class ChannelAllocator {
public:
  ChannelAllocator();

  bool is_note_active(const ym2612::Note &note) const;
  bool note_on(const ym2612::Note &note, ym2612::Device &device);
  bool note_off(const ym2612::Note &note, ym2612::Device &device);
  void release_all(ym2612::Device &device);

  const std::array<bool, 6> &channel_usage() const { return channel_key_on_; }
  const std::map<ym2612::Note, ym2612::ChannelIndex> &active_notes() const {
    return note_to_channel_;
  }

private:
  std::array<bool, 6> channel_key_on_;
  std::map<ym2612::Note, ym2612::ChannelIndex> note_to_channel_;
};

