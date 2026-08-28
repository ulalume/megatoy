#include "../test_check.hpp"
#include "ym2612/note.hpp"

#include <iostream>

namespace {

// MIDI 0..11 is octave -1, which does not fit in Note::octave (uint8_t).
// Regression for a bug where `midi_note / 12 - 1` computed in uint8_t
// arithmetic underflowed to 255 and then clamped (in frequency_with_bend)
// to block 7 -- the highest octave -- instead of the lowest.
void test_lowest_midi_notes_clamp_to_octave_zero() {
  const ym2612::Note lowest_c = ym2612::Note::from_midi_note(0);
  CHECK(lowest_c.octave == 0);
  CHECK(lowest_c.key == Key::C);

  const ym2612::Note lowest_b = ym2612::Note::from_midi_note(11);
  CHECK(lowest_b.octave == 0);
  CHECK(lowest_b.key == Key::B);
}

// MIDI 12 (octave 0, C) is the first note above the wrap-around range and
// must be unaffected by the clamp.
void test_octave_zero_boundary_is_unaffected() {
  const ym2612::Note note = ym2612::Note::from_midi_note(12);
  CHECK(note.octave == 0);
  CHECK(note.key == Key::C);
}

// MIDI 60 is C4 (octave 4) in megatoy's numbering.
void test_middle_c() {
  const ym2612::Note note = ym2612::Note::from_midi_note(60);
  CHECK(note.octave == 4);
  CHECK(note.key == Key::C);
}

// MIDI 127 is the highest MIDI note and must round-trip exactly as before
// the fix -- only notes 0..11 change behavior.
void test_highest_midi_note() {
  const ym2612::Note note = ym2612::Note::from_midi_note(127);
  CHECK(note.octave == 9);
  CHECK(note.key == Key::G);
}

// The clamp must not disturb the block/fnum path that actually drives
// hardware registers: block 0 with the note's canonical fnum.
void test_lowest_notes_produce_block_zero_frequency() {
  const auto lowest_c = ym2612::Note::from_midi_note(0);
  const auto freq = ym2612::frequency_with_bend(lowest_c, 0.0f);
  CHECK(freq.block == 0);
  CHECK(freq.fnum == ym2612::fnote_from_key(Key::C));

  const auto lowest_b = ym2612::Note::from_midi_note(11);
  const auto freq_b = ym2612::frequency_with_bend(lowest_b, 0.0f);
  CHECK(freq_b.block == 0);
  CHECK(freq_b.fnum == ym2612::fnote_from_key(Key::B));
}

} // namespace

int main() {
  test_lowest_midi_notes_clamp_to_octave_zero();
  test_octave_zero_boundary_is_unaffected();
  test_middle_c();
  test_highest_midi_note();
  test_lowest_notes_produce_block_zero_frequency();

  std::cout << "All note tests passed\n";
  return 0;
}
