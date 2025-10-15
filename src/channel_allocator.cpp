#include "channel_allocator.hpp"
#include "ym2612/channel.hpp"
#include "ym2612/device.hpp"

ChannelAllocator::ChannelAllocator() : channel_key_on_{} {}

bool ChannelAllocator::is_note_active(const ym2612::Note &note) const {
  return note_to_channel_.find(note) != note_to_channel_.end();
}

std::optional<ChannelAllocator::ChannelClaim>
ChannelAllocator::note_on(const ym2612::Note &note, bool allow_voice_steal) {
  if (is_note_active(note)) {
    return std::nullopt;
  }

  auto claim_channel = [&](uint8_t index) {
    ym2612::ChannelIndex channel = ym2612::all_channel_indices[index];
    channel_key_on_[index] = true;
    channel_to_note_[index] = note;
    note_to_channel_[note] = channel;
    channel_order_[index] = ++allocation_counter_;
    return ChannelClaim{channel, std::nullopt};
  };

  for (uint8_t i = 0; i < channel_key_on_.size(); ++i) {
    if (!channel_key_on_[i]) {
      return claim_channel(i);
    }
  }

  if (!allow_voice_steal) {
    return std::nullopt;
  }

  uint8_t oldest_index = 0;
  uint64_t oldest_order = channel_order_[0];
  for (uint8_t i = 1; i < channel_order_.size(); ++i) {
    if (channel_order_[i] < oldest_order) {
      oldest_order = channel_order_[i];
      oldest_index = i;
    }
  }

  ym2612::ChannelIndex channel = ym2612::all_channel_indices[oldest_index];
  std::optional<ym2612::Note> replaced_note = channel_to_note_[oldest_index];
  if (replaced_note) {
    note_to_channel_.erase(*replaced_note);
  }

  channel_to_note_[oldest_index] = note;
  note_to_channel_[note] = channel;
  channel_order_[oldest_index] = ++allocation_counter_;
  channel_key_on_[oldest_index] = true;

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
  channel_order_[channel_idx] = 0;
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
