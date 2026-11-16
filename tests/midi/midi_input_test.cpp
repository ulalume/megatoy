// What MIDI input has the engine do: which messages are taken, what each
// becomes, and who holds a note that more than one input plays.

#include "audio/audio_command.hpp"
#include "audio/audio_engine.hpp"
#include "audio/performance.hpp"
#include "midi/midi_backend.hpp"
#include "midi/midi_commands.hpp"
#include "ym2612/note.hpp"
#include "ym2612/patch.hpp"

#include "../test_check.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using audio::AudioCommand;

constexpr uint32_t kSampleRate = 44100;
constexpr uint8_t kKeyboard = 1;
constexpr uint8_t kPads = 2;

const ym2612::Note c4 = ym2612::Note::from_midi_note(60);
const ym2612::Note e4 = ym2612::Note::from_midi_note(64);

/// An engine with a patch in it, and the commands sent to it applied.
struct Rig {
  AudioEngine engine;

  Rig() {
    CHECK(engine.initialize(kSampleRate));
    ym2612::Patch patch;
    patch.instrument.algorithm = 7;
    for (auto &op : patch.instrument.operators) {
      op.attack_rate = 31;
      op.release_rate = 15;
      op.total_level = 20;
      op.multiple = 1;
    }
    send(AudioCommand::apply_patch(patch.global, patch.channel,
                                   patch.instrument));
  }

  void send(const AudioCommand &command) {
    CHECK(engine.submit(command));
    std::vector<int16_t> pcm(64 * 2, 0);
    engine.render(static_cast<uint32_t>(pcm.size() * sizeof(int16_t)),
                  pcm.data());
  }

  void send(const MidiMessage &message) {
    for (const auto &command : midi_commands::from_message(message)) {
      send(command);
    }
  }

  bool sounding(const ym2612::Note &note) const {
    return engine.notes().published_contains(note);
  }

  /// The count of key-ons so far: the newest voice's sequence.
  uint64_t key_ons() const {
    uint64_t newest = 0;
    for (const auto &voice : engine.voice_activity().voices) {
      newest = std::max(newest, voice.sequence);
    }
    return newest;
  }
};

MidiMessage parsed(std::uint8_t status, std::uint8_t data1,
                   std::uint8_t data2) {
  MidiMessage message;
  CHECK(parse_midi_message(status, data1, data2, message));
  return message;
}

void test_messages_are_read_from_their_bytes() {
  // Whatever the channel.
  auto message = parsed(0x93, 60, 100);
  CHECK(message.type == MidiMessage::Type::NoteOn);
  CHECK(message.note == c4);
  CHECK(message.velocity == 100);

  CHECK(parsed(0x80, 60, 40).type == MidiMessage::Type::NoteOff);
  CHECK(parsed(0x90, 60, 0).type == MidiMessage::Type::NoteOff);

  message = parsed(0xe0, 0x7f, 0x7f);
  CHECK(message.type == MidiMessage::Type::PitchBend);
  CHECK(message.pitch_bend == 16383);
  CHECK(parsed(0xe0, 0, 0x40).pitch_bend ==
        audio::performance::kPitchBendCenter);

  message = parsed(0xb0, 64, 127);
  CHECK(message.type == MidiMessage::Type::ControlChange);
  CHECK(message.controller == 64);
  CHECK(message.controller_value == 127);

  // Aftertouch, program changes and system messages are not taken.
  for (const std::uint8_t status : {0xa0, 0xc0, 0xd0, 0xf8}) {
    MidiMessage unused;
    CHECK(!parse_midi_message(status, 1, 2, unused));
  }
}

void test_messages_become_commands() {
  MidiMessage message = parsed(0x90, 60, 100);
  message.source = kPads;
  auto commands = midi_commands::from_message(message);
  CHECK(commands.count == 1);
  CHECK(commands.items[0].type == AudioCommand::Type::NoteOn);
  CHECK(commands.items[0].source == kPads);

  commands = midi_commands::from_message(parsed(0xb0, 1, 90));
  CHECK(commands.count == 1);
  CHECK(commands.items[0].type == AudioCommand::Type::ModWheel);
  CHECK(commands.items[0].mod_wheel_value == 90);

  // The pedal is down from half way.
  commands = midi_commands::from_message(parsed(0xb0, 64, 64));
  CHECK(commands.count == 1);
  CHECK(commands.items[0].type == AudioCommand::Type::Sustain);
  CHECK(commands.items[0].pedal_down);
  CHECK(!midi_commands::from_message(parsed(0xb0, 64, 63))
             .items[0]
             .pedal_down);

  for (const std::uint8_t controller : {120, 123}) {
    commands = midi_commands::from_message(parsed(0xb0, controller, 0));
    CHECK(commands.count == 1);
    CHECK(commands.items[0].type == AudioCommand::Type::AllNotesOff);
  }

  // A controller megatoy has no use for does nothing.
  CHECK(midi_commands::from_message(parsed(0xb0, 7, 100)).count == 0);

  MidiMessage closed;
  closed.type = MidiMessage::Type::SourceClosed;
  closed.source = kPads;
  commands = midi_commands::from_message(closed);
  CHECK(commands.count == 3);
  CHECK(commands.items[0].type == AudioCommand::Type::ReleaseSource);
  CHECK(commands.items[0].source == kPads);
  CHECK(commands.items[1].type == AudioCommand::Type::PitchBend);
  CHECK(commands.items[1].pitch_bend_value ==
        audio::performance::kPitchBendCenter);
  CHECK(commands.items[2].type == AudioCommand::Type::ModWheel);
  CHECK(commands.items[2].mod_wheel_value == 0);
}

void test_a_note_sounds_until_its_last_holder_lets_go() {
  Rig rig;
  rig.send(AudioCommand::note_on(c4, 100, kKeyboard));
  rig.send(AudioCommand::note_on(c4, 100)); // the app's own keyboard
  CHECK(rig.key_ons() == 1);

  rig.send(AudioCommand::note_off(c4));
  CHECK(rig.sounding(c4));
  rig.send(AudioCommand::note_off(c4, kKeyboard));
  CHECK(!rig.sounding(c4));

  // A source that sends the note twice still lets go with one note-off.
  rig.send(AudioCommand::note_on(c4, 100, kKeyboard));
  rig.send(AudioCommand::note_on(c4, 100, kKeyboard));
  rig.send(AudioCommand::note_off(c4, kKeyboard));
  CHECK(!rig.sounding(c4));
}

void test_the_pedal_holds_notes_let_go() {
  Rig rig;
  rig.send(AudioCommand::note_on(c4, 100, kKeyboard));
  rig.send(AudioCommand::sustain(kKeyboard, true));
  rig.send(AudioCommand::note_off(c4, kKeyboard));
  CHECK(rig.sounding(c4));

  // Struck again under the pedal, it starts anew rather than being ignored.
  const auto before = rig.key_ons();
  rig.send(AudioCommand::note_on(c4, 100, kKeyboard));
  CHECK(rig.key_ons() == before + 1);
  CHECK(rig.sounding(c4));

  // The pedal comes up under a held key: the key still holds it.
  rig.send(AudioCommand::sustain(kKeyboard, false));
  CHECK(rig.sounding(c4));
  rig.send(AudioCommand::note_off(c4, kKeyboard));
  CHECK(!rig.sounding(c4));

  // One input's pedal holds another's notes, until the last pedal is up.
  rig.send(AudioCommand::sustain(kKeyboard, true));
  rig.send(AudioCommand::sustain(kPads, true));
  rig.send(AudioCommand::note_on(e4, 100, kPads));
  rig.send(AudioCommand::note_off(e4, kPads));
  rig.send(AudioCommand::sustain(kPads, false));
  CHECK(rig.sounding(e4));
  rig.send(AudioCommand::sustain(kKeyboard, false));
  CHECK(!rig.sounding(e4));
}

void test_an_input_that_goes_takes_only_what_it_held() {
  Rig rig;
  rig.send(AudioCommand::note_on(c4, 100, kKeyboard));
  rig.send(AudioCommand::note_on(e4, 100, kPads));
  rig.send(AudioCommand::pitch_bend(12000));

  MidiMessage closed;
  closed.type = MidiMessage::Type::SourceClosed;
  closed.source = kKeyboard;
  rig.send(closed);
  CHECK(!rig.sounding(c4));
  CHECK(rig.sounding(e4));

  // Its pedal goes with it, so the next note-off is heard.
  rig.send(AudioCommand::sustain(kKeyboard, true));
  rig.send(closed);
  rig.send(AudioCommand::note_off(e4, kPads));
  CHECK(!rig.sounding(e4));
}

void test_all_notes_off_lifts_keys_and_pedals() {
  Rig rig;
  rig.send(AudioCommand::sustain(kKeyboard, true));
  rig.send(AudioCommand::note_on(c4, 100, kKeyboard));
  rig.send(parsed(0xb0, 123, 0));
  CHECK(!rig.sounding(c4));

  rig.send(AudioCommand::note_on(e4, 100, kKeyboard));
  rig.send(AudioCommand::note_off(e4, kKeyboard));
  CHECK(!rig.sounding(e4));
}

} // namespace

int main() {
  test_messages_are_read_from_their_bytes();
  test_messages_become_commands();
  test_a_note_sounds_until_its_last_holder_lets_go();
  test_the_pedal_holds_notes_let_go();
  test_an_input_that_goes_takes_only_what_it_held();
  test_all_notes_off_lifts_keys_and_pedals();
  std::cout << "All MIDI input tests passed\n";
  return 0;
}
