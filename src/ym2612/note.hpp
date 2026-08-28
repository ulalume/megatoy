#pragma once

#include "core/types.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>

namespace ym2612 {
struct Note;
std::ostream &operator<<(std::ostream &os, const Note &note);

struct NoteFrequency {
  uint16_t fnum;
  uint8_t block;
};

inline constexpr uint16_t kCanonicalFnumMin = 322;
inline constexpr uint16_t kCanonicalFnumEnd = kCanonicalFnumMin * 2;
inline constexpr uint16_t kHardwareFnumMax = 0x7ff;

inline uint16_t fnote_from_key(Key key) {
  switch (key) {
  case Key::C:
    return 322;
  case Key::C_SHARP:
    return 341;
  case Key::D:
    return 361;
  case Key::D_SHARP:
    return 383;
  case Key::E:
    return 406;
  case Key::F:
    return 430;
  case Key::F_SHARP:
    return 455;
  case Key::G:
    return 482;
  case Key::G_SHARP:
    return 511;
  case Key::A:
    return 541;
  case Key::A_SHARP:
    return 574;
  case Key::B:
    return 608;
  }
  return 322;
}

struct Note {
  uint8_t octave;
  Key key;

  bool operator<(const Note &other) const {
    return this->frequency() < other.frequency();
  };
  bool operator>(const Note &other) const { return other < *this; };
  bool operator==(const Note &other) const {
    return octave == other.octave && key == other.key;
  };
  bool operator!=(const Note &other) const { return !(*this == other); };
  bool operator<=(const Note &other) const { return !(*this > other); };
  bool operator>=(const Note &other) const { return !(*this < other); };

  static Note from_midi_note(uint8_t midi_note) {
    // Signed so MIDI 0-11 (octave -1) clamps to block 0 instead of
    // underflowing a uint8_t to 255 and landing on the highest block.
    int octave = midi_note / 12 - 1;
    if (octave < 0) {
      octave = 0;
    }
    uint8_t note = midi_note % 12;
    return Note{static_cast<uint8_t>(octave), all_keys[note]};
  }
  uint8_t midi_note() const {
    return (octave + 1) * 12 + static_cast<uint8_t>(key);
  }
  std::string name() const {
    std::stringstream key_name;
    key_name << *this;
    return key_name.str();
  }
  int frequency() const { return fnote_from_key(key) * (1 << octave); }
};

// Bend is evaluated only on note-on and controller changes, not per sample.
// std::exp2 plus nearest-integer F-num rounding stays within 3.1 cents across
// the supported +/-2-semitone bend range. The canonical octave window keeps
// equivalent frequencies near the note table's precision; at blocks 0 and 7
// the full hardware F-num range preserves bends that cannot be renormalized.
inline NoteFrequency frequency_with_bend(const Note &note,
                                         float bend_semitones) {
  int block = std::clamp<int>(note.octave, 0, 7);
  double fnum = static_cast<double>(fnote_from_key(note.key)) *
                std::exp2(static_cast<double>(bend_semitones) / 12.0);

  while (fnum >= kCanonicalFnumEnd && block < 7) {
    fnum *= 0.5;
    ++block;
  }
  while (fnum < kCanonicalFnumMin && block > 0) {
    fnum *= 2.0;
    --block;
  }

  const auto rounded = static_cast<int>(std::lround(fnum));
  return {static_cast<uint16_t>(
              std::clamp(rounded, 0, static_cast<int>(kHardwareFnumMax))),
          static_cast<uint8_t>(block)};
}
inline std::ostream &operator<<(std::ostream &os, const Note &note) {
  return os << note.key << static_cast<int>(note.octave);
}

}; // namespace ym2612
