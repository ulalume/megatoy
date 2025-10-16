#pragma once

#include "core/types.hpp"
#include <cstdint>
#include <iostream>

namespace ym2612 {

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
    auto a = fnote_from_key(key) * (1 << octave);
    auto b = fnote_from_key(other.key) * (1 << other.octave);
    return a < b;
  };
  bool operator>(const Note &other) const { return other < *this; };
  bool operator==(const Note &other) const {
    return octave == other.octave && key == other.key;
  };
  bool operator!=(const Note &other) const { return !(*this == other); };
  bool operator<=(const Note &other) const { return !(*this > other); };
  bool operator>=(const Note &other) const { return !(*this < other); };

  static Note from_midi_note(uint8_t midi_note) {
    uint8_t block = midi_note / 12 - 1;
    uint8_t note = midi_note % 12;
    return Note{block, all_keys[note]};
  }
};

inline std::ostream &operator<<(std::ostream &os, const Note &note) {
  return os << note.key << static_cast<int>(note.octave);
}

}; // namespace ym2612
