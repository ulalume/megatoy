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

  std::optional<size_t> free_index;
  for (size_t idx = 0; idx < channel_key_on_.size(); ++idx) {
    if (channel_key_on_[idx])
      continue;
    if (!free_index || channel_order_[idx] < channel_order_[*free_index]) {
      free_index = idx;
    }
  }

  size_t selected_index;
  std::optional<ym2612::Note> replaced_note;

  if (free_index) {
    selected_index = *free_index;
    replaced_note.reset();
  } else {
    if (!allow_voice_steal)
      return std::nullopt;

    auto oldest_it =
        std::min_element(channel_order_.begin(), channel_order_.end());
    selected_index = static_cast<size_t>(oldest_it - channel_order_.begin());
    replaced_note = channel_to_note_[selected_index];
    if (replaced_note)
      note_to_channel_.erase(*replaced_note);
  }

  auto channel = ym2612::all_channel_indices[selected_index];
  channel_key_on_[selected_index] = true;
  channel_to_note_[selected_index] = note;
  note_to_channel_[note] = channel;
  channel_order_[selected_index] = ++allocation_counter_;

  return ChannelClaim{channel, replaced_note};
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
