#pragma once

#include "ym2612/note.hpp"
#include "ym2612/types.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace ym2612 {
class Device;
} // namespace ym2612

/**
 * One channel's note as the envelope graph needs to see it: which note, when
 * the key went down, and when -- if at all -- it came back up.
 *
 * `sequence` counts key-ons across the whole allocator, so it is both a
 * per-channel generation (a steal writes a strictly greater value on the same
 * channel, which is how the UI tells "same channel, different note" from
 * "same note still going") and a global recency order (the largest sequence
 * is the most recently started voice). Zero means the channel has never
 * sounded.
 *
 * The record OUTLIVES the note-off, unlike published_notes(): a released
 * voice is still falling through its release, and the graph draws that.
 * Only a new key-on on the same channel replaces it.
 */
struct VoiceActivity {
  uint64_t sequence = 0;
  uint64_t key_on_sample = 0;
  /// Only meaningful while `held` is false.
  uint64_t key_off_sample = 0;
  uint8_t midi_note = 0;
  bool held = false;

  bool valid() const { return sequence != 0; }
};

/**
 * The six voices plus the chip's clock, read in one go so every voice is
 * timed against the same instant. `now_samples` and the key stamps are all
 * output frames rendered since the engine started.
 */
struct VoiceActivityFrame {
  std::array<VoiceActivity, 6> voices{};
  uint64_t now_samples = 0;
  uint32_t sample_rate = 44100;
};

/**
 * Decides which of the chip's six channels a note plays on.
 *
 * A note is held by every source that has its key down -- the app's own
 * keyboards are one, each MIDI input another -- and comes up when the last of
 * them lets go, or stays until the sustain pedal does while one is down.
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
  /// `sample_position` is where the key-on reaches the chip, in rendered
  /// output frames; it is published for the graph and nothing else. There is
  /// no default: a caller that left it out would stamp its voice at t = 0 and
  /// the graph would draw a confident, wrong cursor with nothing to say so.
  /// Nothing is claimed for a note a key already holds down: `source` holds
  /// it too. One only the pedal holds is struck again, on its own channel.
  std::optional<ChannelClaim> note_on(const ym2612::Note &note,
                                      uint8_t velocity, bool allow_voice_steal,
                                      uint64_t sample_position,
                                      uint8_t source = 0);
  std::optional<ym2612::Note> active_note(ym2612::ChannelIndex channel) const;
  std::optional<uint8_t> active_velocity(ym2612::ChannelIndex channel) const;
  /// True when the note came up: no other source and no pedal holds it.
  bool note_off(const ym2612::Note &note, ym2612::Device &device,
                uint64_t sample_position, uint8_t source = 0);
  /// `source` lets go of every note and of its pedal.
  void release_source(uint8_t source, ym2612::Device &device,
                      uint64_t sample_position);
  /// `source`'s sustain pedal. The notes no key holds come up with the last
  /// pedal.
  void set_sustain(uint8_t source, bool pedal_down, ym2612::Device &device,
                   uint64_t sample_position);
  /// Every note comes up and every pedal counts as up.
  void release_all(ym2612::Device &device, uint64_t sample_position);

  // --- readable from any thread ---

  /// Notes currently sounding, in channel order.
  std::vector<ym2612::Note> published_notes() const;
  /// The same into `out`, without allocating; returns how many there are.
  std::size_t published_notes(std::array<ym2612::Note, 6> &out) const;
  bool published_contains(const ym2612::Note &note) const;

  /**
   * What every channel is playing, or was playing until its key came up.
   *
   * Each record is read through a seqlock, so a caller never sees one voice's
   * note paired with another's timestamps; see published_voice() for why that
   * is sound.
   */
  std::array<VoiceActivity, 6> published_voices() const;

private:
  void publish();
  /// The key coming up on one channel: it stops sounding, but its voice keeps
  /// its record, because it is still falling through its release.
  void release_channel(uint8_t index, uint64_t sample_position);
  void publish_voice(std::size_t index, const VoiceActivity &voice);
  VoiceActivity published_voice(std::size_t index) const;
  /// The notes neither a key nor a pedal holds come up.
  void release_unheld(ym2612::Device &device, uint64_t sample_position);

  std::array<bool, 6> channel_key_on_;
  std::array<std::optional<ym2612::Note>, 6> channel_to_note_;
  std::array<uint8_t, 6> channel_velocity_{};
  /// A bit for each source with the channel's key down.
  std::array<uint32_t, 6> channel_holders_{};
  /// A bit for each source with its pedal down.
  uint32_t sustaining_ = 0;
  std::array<uint64_t, 6> channel_order_{};
  uint64_t allocation_counter_ = 0;
  /// Never reset, unlike allocation_counter_: the UI orders voices by it and
  /// compares it against what it saw last frame, so it has to be monotone for
  /// the life of the process.
  uint64_t voice_counter_ = 0;
  std::map<ym2612::Note, ym2612::ChannelIndex> note_to_channel_;
  /// The audio thread's own copy of what it last published, so a key-off can
  /// amend a record without reading its own publication back.
  std::array<VoiceActivity, 6> voice_state_{};

  // MIDI note number plus one; zero means the channel is idle.
  std::array<std::atomic<uint16_t>, 6> published_{};

  /**
   * One channel's VoiceActivity, published as a seqlock.
   *
   * `generation` is odd while a write is in progress and even between writes.
   * Every payload field is its own atomic so a reader that catches a write in
   * flight reads a stale value rather than committing undefined behaviour --
   * a memcpy-style seqlock over plain members would be a data race.
   */
  struct PublishedVoice {
    std::atomic<uint32_t> generation{0};
    std::atomic<uint64_t> sequence{0};
    std::atomic<uint64_t> key_on{0};
    std::atomic<uint64_t> key_off{0};
    std::atomic<uint32_t> note{0};
    std::atomic<uint32_t> held{0};
  };
  std::array<PublishedVoice, 6> voices_{};
};
