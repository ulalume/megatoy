#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <string>
#include <vector>

enum class Key : uint8_t {
  C = 0,
  C_SHARP,
  D,
  D_SHARP,
  E,
  F,
  F_SHARP,
  G,
  G_SHARP,
  A,
  A_SHARP,
  B,
};

inline const Key shift_key(Key key, uint8_t shift) {
  return static_cast<Key>((static_cast<uint8_t>(key) + shift) % 12);
}

inline const std::array<Key, 12> all_keys = {
    Key::C, Key::C_SHARP, Key::D, Key::D_SHARP, Key::E, Key::F, Key::F_SHARP,
    Key::G, Key::G_SHARP, Key::A, Key::A_SHARP, Key::B,
};

inline const char *key_names[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};

inline std::ostream &operator<<(std::ostream &os, const Key &key) {
  switch (key) {
  case Key::C:
    return os << "C";
  case Key::C_SHARP:
    return os << "C#";
  case Key::D:
    return os << "D";
  case Key::D_SHARP:
    return os << "D#";
  case Key::E:
    return os << "E";
  case Key::F:
    return os << "F";
  case Key::F_SHARP:
    return os << "F#";
  case Key::G:
    return os << "G";
  case Key::G_SHARP:
    return os << "G#";
  case Key::A:
    return os << "A";
  case Key::A_SHARP:
    return os << "A#";
  case Key::B:
    return os << "B";
  default:
    return os << "<unknown key>";
  }
}

enum class Scale {
  CHROMATIC,
  MAJOR,
  MINOR,

  HARMONIC_MINOR,

  DORIAN,
  PHRYGIAN,
  LYDIAN,
  MIXOLYDIAN,
  LOCRIAN,

  MAJAR_BLUES,
  MINOR_BLUES,

  MAJAR_PENTATONIC,
  MINOR_PENTATONIC,
  WHOLE_TONE,
  RYUKYU,
};

inline const char *scale_names[] = {
    "Chromatic",        "Major",       "Minor",       "Harmonic Minor",
    "Dorian",           "Phrygian",    "Lydian",      "Mixolydian",
    "Locrian",          "Major Blues", "Minor Blues", "Major Pentatonic",
    "Minor Pentatonic", "Whole Tone",  "Ryukyu",
};

inline std::vector<Key> keys_from_scale_and_key(Scale scale, Key key) {
  // Semitones above the key, in the order of Scale. Chromatic is every key
  // whatever the selected one, and so is a value outside the enum.
  static const std::vector<uint8_t> offsets[] = {
      {},                     // Chromatic
      {0, 2, 4, 5, 7, 9, 11}, // Major
      {0, 2, 3, 5, 7, 8, 10}, // Minor
      {0, 2, 3, 5, 7, 8, 11}, // Harmonic Minor
      {0, 2, 3, 5, 7, 9, 10}, // Dorian
      {0, 1, 3, 5, 7, 8, 10}, // Phrygian
      {0, 2, 4, 6, 7, 9, 11}, // Lydian
      {0, 2, 4, 5, 7, 9, 10}, // Mixolydian
      {0, 1, 3, 5, 6, 8, 10}, // Locrian
      {0, 2, 3, 4, 7, 9},     // Major Blues
      {0, 3, 5, 6, 7, 10},    // Minor Blues
      {0, 2, 4, 7, 9},        // Major Pentatonic
      {0, 3, 5, 7, 10},       // Minor Pentatonic
      {0, 2, 4, 6, 8, 10},    // Whole Tone
      {0, 4, 5, 7, 11},       // Ryukyu
  };
  const auto index = static_cast<std::size_t>(scale);
  if (scale == Scale::CHROMATIC || index >= std::size(offsets)) {
    return {all_keys.begin(), all_keys.end()};
  }
  std::vector<Key> keys;
  keys.reserve(offsets[index].size());
  for (const auto offset : offsets[index]) {
    keys.push_back(
        shift_key(static_cast<Key>(offset), static_cast<uint8_t>(key)));
  }
  return keys;
}
