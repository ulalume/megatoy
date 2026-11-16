#pragma once

#include "audio/audio_command.hpp"
#include "audio/performance.hpp"
#include "midi/midi_backend.hpp"

#include <array>
#include <cstddef>

/// What a MIDI message has the audio engine do.
namespace midi_commands {

inline constexpr std::uint8_t kModWheel = 1;
inline constexpr std::uint8_t kSustainPedal = 64;
inline constexpr std::uint8_t kAllSoundOff = 120;
inline constexpr std::uint8_t kAllNotesOff = 123;

/// A few commands, without allocating: this runs on the driver's thread.
struct Commands {
  std::array<audio::AudioCommand, 3> items{};
  std::size_t count = 0;

  void add(const audio::AudioCommand &command) { items[count++] = command; }
  const audio::AudioCommand *begin() const { return items.data(); }
  const audio::AudioCommand *end() const { return items.data() + count; }
};

inline Commands from_message(const MidiMessage &message) {
  using audio::AudioCommand;
  Commands commands;
  switch (message.type) {
  case MidiMessage::Type::NoteOn:
    commands.add(AudioCommand::note_on(message.note, message.velocity,
                                       message.source));
    break;
  case MidiMessage::Type::NoteOff:
    commands.add(AudioCommand::note_off(message.note, message.source));
    break;
  case MidiMessage::Type::PitchBend:
    commands.add(AudioCommand::pitch_bend(message.pitch_bend));
    break;
  case MidiMessage::Type::ControlChange:
    if (message.controller == kModWheel) {
      commands.add(AudioCommand::mod_wheel(message.controller_value));
    } else if (message.controller == kSustainPedal) {
      // Down from half way, as the pedal's switch is defined.
      commands.add(AudioCommand::sustain(message.source,
                                         message.controller_value >= 64));
    } else if (message.controller == kAllSoundOff ||
               message.controller == kAllNotesOff) {
      commands.add(AudioCommand::all_notes_off());
    }
    break;
  case MidiMessage::Type::SourceClosed:
    // Its notes and pedal are let go. The wheels are the engine's, not the
    // input's, so they go back to rest for whoever plays next.
    commands.add(AudioCommand::release_source(message.source));
    commands.add(
        AudioCommand::pitch_bend(audio::performance::kPitchBendCenter));
    commands.add(AudioCommand::mod_wheel(0));
    break;
  }
  return commands;
}

} // namespace midi_commands
