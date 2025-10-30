#include "channel_allocator.hpp"
#include "ym2612/channel.hpp"
#include "ym2612/device.hpp"
#include <algorithm>

ChannelAllocator::ChannelAllocator() : channel_key_on_{} {}

bool ChannelAllocator::is_note_active(const ym2612::Note &note) const {
  return note_to_channel_.find(note) != note_to_channel_.end();
}

std::optional<ChannelAllocator::ChannelClaim>
ChannelAllocator::note_on(const ym2612::Note &note, bool allow_voice_steal) {
  if (is_note_active(note))
    return std::nullopt;

  const auto oldest_it =
      std::min_element(channel_order_.begin(), channel_order_.end());
  const uint8_t oldest_index =
      static_cast<uint8_t>(oldest_it - channel_order_.begin());
  const bool in_use = channel_key_on_[oldest_index];
  const auto replaced_note = channel_to_note_[oldest_index];

  if (in_use && !allow_voice_steal)
    return std::nullopt;

  if (in_use && replaced_note)
    note_to_channel_.erase(*replaced_note);

  auto channel = ym2612::all_channel_indices[oldest_index];
  channel_key_on_[oldest_index] = true;
  channel_to_note_[oldest_index] = note;
  note_to_channel_[note] = channel;
  channel_order_[oldest_index] = ++allocation_counter_;

  return ChannelClaim{channel, (in_use ? replaced_note : std::nullopt)};
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
  channel_to_note_[channel_idx].reset();
  note_to_channel_.erase(it);
  return true;
}

void ChannelAllocator::release_all(ym2612::Device &device) {
  for (const auto &[note, channel] : note_to_channel_) {
    device.channel(channel).write_key_off();
    auto channel_idx = static_cast<uint8_t>(channel);
    channel_key_on_[channel_idx] = false;
    channel_to_note_[channel_idx].reset();
    channel_order_[channel_idx] = 0;
  }
  note_to_channel_.clear();
  allocation_counter_ = 0;
}
