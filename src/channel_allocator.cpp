#include "channel_allocator.hpp"
#include "ym2612/channel.hpp"
#include "ym2612/device.hpp"
#include <algorithm>

ChannelAllocator::ChannelAllocator() : channel_key_on_{} { publish(); }

void ChannelAllocator::publish() {
  for (std::size_t index = 0; index < published_.size(); ++index) {
    const auto &note = channel_to_note_[index];
    const uint16_t value = (note && channel_key_on_[index])
                               ? static_cast<uint16_t>(note->midi_note() + 1)
                               : 0;
    published_[index].store(value, std::memory_order_release);
  }
}

std::vector<ym2612::Note> ChannelAllocator::published_notes() const {
  std::vector<ym2612::Note> notes;
  for (const auto &slot : published_) {
    const uint16_t value = slot.load(std::memory_order_acquire);
    if (value != 0) {
      notes.push_back(
          ym2612::Note::from_midi_note(static_cast<uint8_t>(value - 1)));
    }
  }
  return notes;
}

bool ChannelAllocator::published_contains(const ym2612::Note &note) const {
  const uint16_t wanted = static_cast<uint16_t>(note.midi_note() + 1);
  for (const auto &slot : published_) {
    if (slot.load(std::memory_order_acquire) == wanted) {
      return true;
    }
  }
  return false;
}

std::array<bool, 6> ChannelAllocator::published_channels() const {
  std::array<bool, 6> busy{};
  for (std::size_t index = 0; index < published_.size(); ++index) {
    busy[index] = published_[index].load(std::memory_order_acquire) != 0;
  }
  return busy;
}

bool ChannelAllocator::is_note_active(const ym2612::Note &note) const {
  return note_to_channel_.find(note) != note_to_channel_.end();
}

std::optional<ChannelAllocator::ChannelClaim>
ChannelAllocator::note_on(const ym2612::Note &note, uint8_t velocity,
                          bool allow_voice_steal) {
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
  channel_velocity_[selected_index] = velocity;
  note_to_channel_[note] = channel;
  channel_order_[selected_index] = ++allocation_counter_;

  publish();
  return ChannelClaim{channel, replaced_note};
}

std::optional<uint8_t>
ChannelAllocator::active_velocity(ym2612::ChannelIndex channel) const {
  const auto index = static_cast<uint8_t>(channel);
  if (!channel_key_on_[index]) {
    return std::nullopt;
  }
  return channel_velocity_[index];
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
  channel_velocity_[channel_idx] = 0;
  note_to_channel_.erase(it);

  publish();
  return true;
}

void ChannelAllocator::release_all(ym2612::Device &device) {
  for (const auto &[note, channel] : note_to_channel_) {
    device.channel(channel).write_key_off();
    auto channel_idx = static_cast<uint8_t>(channel);
    channel_key_on_[channel_idx] = false;
    channel_to_note_[channel_idx].reset();
    channel_velocity_[channel_idx] = 0;
    channel_order_[channel_idx] = 0;
  }
  note_to_channel_.clear();
  allocation_counter_ = 0;

  publish();
}
