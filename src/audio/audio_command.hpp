#pragma once

#include "ym2612/note.hpp"
#include "ym2612/types.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace audio {

/**
 * Something to do to the chip, handed to the audio thread.
 *
 * Every chip write -- notes and patch edits alike -- goes through here, so
 * exactly one thread ever touches the chip. Before this, notes were applied
 * from the UI frame loop while rendering ran on the audio thread, which
 * quantized note timing to the render rate and raced with the renderer.
 *
 * A note carries only pitch and velocity. The instrument to play it with is
 * whatever the engine last received, which keeps this small enough to fill in
 * from a MIDI driver callback without reading the UI's patch.
 */
struct AudioCommand {
  enum class Type : uint8_t {
    NoteOn,
    NoteOff,
    AllNotesOff,
    ApplyPatch,
    PitchBend,
    ModWheel,
  };

  Type type = Type::AllNotesOff;
  ym2612::Note note{};
  uint8_t velocity = 0;
  // Performance commands only. Pitch bend is raw MIDI 14-bit (8192 center),
  // while mod wheel is the MIDI CC1 value (0..127).
  uint16_t pitch_bend_value = 8192;
  uint8_t mod_wheel_value = 0;

  // ApplyPatch only. The patch *name* is deliberately absent: it never
  // reaches a register, and keeping the command trivially copyable keeps the
  // queue free of allocation.
  ym2612::GlobalSettings global{};
  ym2612::ChannelSettings channel{};
  ym2612::ChannelInstrument instrument{};

  static AudioCommand note_on(ym2612::Note note, uint8_t velocity) {
    AudioCommand command;
    command.type = Type::NoteOn;
    command.note = note;
    command.velocity = velocity;
    return command;
  }

  static AudioCommand note_off(ym2612::Note note) {
    AudioCommand command;
    command.type = Type::NoteOff;
    command.note = note;
    return command;
  }

  static AudioCommand all_notes_off() {
    AudioCommand command;
    command.type = Type::AllNotesOff;
    return command;
  }

  static AudioCommand apply_patch(const ym2612::GlobalSettings &global,
                                  const ym2612::ChannelSettings &channel,
                                  const ym2612::ChannelInstrument &instrument) {
    AudioCommand command;
    command.type = Type::ApplyPatch;
    command.global = global;
    command.channel = channel;
    command.instrument = instrument;
    return command;
  }

  static AudioCommand pitch_bend(uint16_t value) {
    AudioCommand command;
    command.type = Type::PitchBend;
    command.pitch_bend_value = value;
    return command;
  }

  static AudioCommand mod_wheel(uint8_t value) {
    AudioCommand command;
    command.type = Type::ModWheel;
    command.mod_wheel_value = value;
    return command;
  }
};

/**
 * Single-producer, single-consumer ring buffer.
 *
 * push() never blocks and never allocates, so it is safe to call from a MIDI
 * driver callback. It drops the command and reports false when full, which
 * takes a whole buffer's worth of unprocessed input to happen.
 *
 * One queue per producer: the engine keeps a separate instance for the UI and
 * for MIDI and drains both, which avoids needing a multi-producer design for
 * the sake of two well-known writers.
 */
class AudioCommandQueue {
public:
  explicit AudioCommandQueue(std::size_t capacity = 256)
      : buffer_(capacity), capacity_(capacity), write_(0), read_(0) {}

  bool push(const AudioCommand &command) {
    const std::size_t write = write_.load(std::memory_order_relaxed);
    const std::size_t next = (write + 1) % capacity_;
    if (next == read_.load(std::memory_order_acquire)) {
      return false; // full
    }
    buffer_[write] = command;
    write_.store(next, std::memory_order_release);
    return true;
  }

  bool pop(AudioCommand &out) {
    const std::size_t read = read_.load(std::memory_order_relaxed);
    if (read == write_.load(std::memory_order_acquire)) {
      return false; // empty
    }
    out = buffer_[read];
    read_.store((read + 1) % capacity_, std::memory_order_release);
    return true;
  }

  bool empty() const {
    return read_.load(std::memory_order_acquire) ==
           write_.load(std::memory_order_acquire);
  }

private:
  std::vector<AudioCommand> buffer_;
  std::size_t capacity_;
  std::atomic<std::size_t> write_;
  std::atomic<std::size_t> read_;
};

} // namespace audio
