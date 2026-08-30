#include "channel_allocator.hpp"
#include "ym2612/channel.hpp"
#include "ym2612/device.hpp"
#include <algorithm>

namespace {

/**
 * How many times a reader retries a channel's seqlock before giving up.
 *
 * The writer's critical section is five relaxed stores, so a reader spinning
 * in a tight loop loses a race only if the audio thread is descheduled in the
 * middle of them. The bound is here so a UI frame cannot be hung by that, not
 * because it is expected to be reached; giving up reports the channel as
 * never having sounded, which costs one frame of one cursor.
 */
constexpr int kSeqlockAttempts = 64;

} // namespace

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

/**
 * The write half of the seqlock. Only ever called from the audio thread --
 * AudioEngine::apply() runs there, or inline on the caller's thread while
 * nothing is rendering at all -- so there is exactly one writer and no
 * writer-writer exclusion is needed.
 *
 * The generation goes odd before any payload store and even after all of
 * them. The release fence after the odd store keeps the payload stores from
 * becoming visible before it; the release on the even store keeps them from
 * becoming visible after it. A reader that sees an odd generation, or two
 * different generations either side of its own reads, therefore knows a write
 * straddled it and retries.
 */
void ChannelAllocator::publish_voice(std::size_t index,
                                     const VoiceActivity &voice) {
  PublishedVoice &slot = voices_[index];
  const uint32_t generation = slot.generation.load(std::memory_order_relaxed);
  slot.generation.store(generation + 1, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_release);
  slot.sequence.store(voice.sequence, std::memory_order_relaxed);
  slot.key_on.store(voice.key_on_sample, std::memory_order_relaxed);
  slot.key_off.store(voice.key_off_sample, std::memory_order_relaxed);
  slot.note.store(voice.midi_note, std::memory_order_relaxed);
  slot.held.store(voice.held ? 1u : 0u, std::memory_order_relaxed);
  slot.generation.store(generation + 2, std::memory_order_release);
}

VoiceActivity ChannelAllocator::published_voice(std::size_t index) const {
  const PublishedVoice &slot = voices_[index];
  for (int attempt = 0; attempt < kSeqlockAttempts; ++attempt) {
    const uint32_t before = slot.generation.load(std::memory_order_acquire);
    if ((before & 1u) != 0u) {
      continue; // a write is in flight
    }
    VoiceActivity voice;
    voice.sequence = slot.sequence.load(std::memory_order_relaxed);
    voice.key_on_sample = slot.key_on.load(std::memory_order_relaxed);
    voice.key_off_sample = slot.key_off.load(std::memory_order_relaxed);
    voice.midi_note =
        static_cast<uint8_t>(slot.note.load(std::memory_order_relaxed));
    voice.held = slot.held.load(std::memory_order_relaxed) != 0;
    // Pairs with the release on the generation store above: the payload loads
    // cannot be sunk past this, so the second generation read really does
    // bracket them.
    std::atomic_thread_fence(std::memory_order_acquire);
    if (slot.generation.load(std::memory_order_relaxed) == before) {
      return voice;
    }
  }
  return VoiceActivity{};
}

std::array<VoiceActivity, 6> ChannelAllocator::published_voices() const {
  std::array<VoiceActivity, 6> out{};
  for (std::size_t index = 0; index < voices_.size(); ++index) {
    out[index] = published_voice(index);
  }
  return out;
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
                          bool allow_voice_steal, uint64_t sample_position) {
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

  // A steal overwrites the record rather than releasing it: the chip is
  // key-offed and keyed on again in the same drain, so the voice that was
  // there has no release to draw.
  VoiceActivity voice;
  voice.sequence = ++voice_counter_;
  voice.key_on_sample = sample_position;
  voice.midi_note = note.midi_note();
  voice.held = true;
  voice_state_[selected_index] = voice;
  publish_voice(selected_index, voice);

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

std::optional<ym2612::Note>
ChannelAllocator::active_note(ym2612::ChannelIndex channel) const {
  const auto index = static_cast<uint8_t>(channel);
  if (!channel_key_on_[index]) {
    return std::nullopt;
  }
  return channel_to_note_[index];
}

void ChannelAllocator::release_channel(uint8_t index,
                                       uint64_t sample_position) {
  channel_key_on_[index] = false;
  channel_to_note_[index].reset();
  channel_velocity_[index] = 0;

  // The voice keeps its record: it is releasing, not gone.
  VoiceActivity &voice = voice_state_[index];
  if (voice.valid()) {
    voice.held = false;
    voice.key_off_sample = sample_position;
    publish_voice(index, voice);
  }
}

bool ChannelAllocator::note_off(const ym2612::Note &note,
                                ym2612::Device &device,
                                uint64_t sample_position) {
  auto it = note_to_channel_.find(note);
  if (it == note_to_channel_.end()) {
    return false;
  }

  ym2612::ChannelIndex channel = it->second;
  device.channel(channel).write_key_off();

  release_channel(static_cast<uint8_t>(channel), sample_position);
  note_to_channel_.erase(it);

  publish();
  return true;
}

void ChannelAllocator::release_all(ym2612::Device &device,
                                   uint64_t sample_position) {
  for (const auto &[note, channel] : note_to_channel_) {
    device.channel(channel).write_key_off();
    const auto channel_idx = static_cast<uint8_t>(channel);
    release_channel(channel_idx, sample_position);
    // Unlike a single note-off, which leaves the freed channel its place in
    // the recency order: the counter goes back to zero here, so the orders
    // have to as well.
    channel_order_[channel_idx] = 0;
  }
  note_to_channel_.clear();
  allocation_counter_ = 0;

  publish();
}
