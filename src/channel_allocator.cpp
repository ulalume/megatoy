#include "channel_allocator.hpp"
#include "ym2612/channel.hpp"
#include "ym2612/device.hpp"

ChannelAllocator::ChannelAllocator() : channel_key_on_{} {}

bool ChannelAllocator::is_note_active(const ym2612::Note &note) const {
  return note_to_channel_.find(note) != note_to_channel_.end();
}

bool ChannelAllocator::note_on(const ym2612::Note &note,
                               ym2612::Device &device) {
  if (is_note_active(note)) {
    return false;
  }

  for (uint8_t i = 0; i < channel_key_on_.size(); ++i) {
    if (!channel_key_on_[i]) {
      ym2612::ChannelIndex channel = ym2612::all_channel_indices[i];
      channel_key_on_[i] = true;
      note_to_channel_[note] = channel;

      device.channel(channel).write_frequency(note);
      device.channel(channel).write_key_on();
      return true;
    }
  }

  return false;
}

bool ChannelAllocator::note_off(const ym2612::Note &note,
                                ym2612::Device &device) {
  auto it = note_to_channel_.find(note);
  if (it == note_to_channel_.end()) {
    return false;
  }

  ym2612::ChannelIndex channel = it->second;
  device.channel(channel).write_key_off();

  auto channel_idx = static_cast<uint8_t>(channel);
  channel_key_on_[channel_idx] = false;
  note_to_channel_.erase(it);
  return true;
}

void ChannelAllocator::release_all(ym2612::Device &device) {
  for (const auto &[note, channel] : note_to_channel_) {
    device.channel(channel).write_key_off();
    auto channel_idx = static_cast<uint8_t>(channel);
    channel_key_on_[channel_idx] = false;
  }
  note_to_channel_.clear();
}
