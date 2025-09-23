#pragma once

#include <array>
#include <cstring>
#include <imgui.h>
#include <vector>
#include <string>
#include <ostream>

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

inline std::string name_key(Key key) {
  return key_names[static_cast<uint8_t>(key)];
}

inline std::ostream &operator<<(std::ostream &os, const Key &key) {
    switch (key) {
        case Key::C:        return os << "C";
        case Key::C_SHARP:  return os << "C#";
        case Key::D:        return os << "D";
        case Key::D_SHARP:  return os << "D#";
        case Key::E:        return os << "E";
        case Key::F:        return os << "F";
        case Key::F_SHARP:  return os << "F#";
        case Key::G:        return os << "G";
        case Key::G_SHARP:  return os << "G#";
        case Key::A:        return os << "A";
        case Key::A_SHARP:  return os << "A#";
        case Key::B:        return os << "B";
        default:            return os << "<unknown key>";
    }
}

enum class Scale {
  MAJOR,
  MINOR,
  CHROMATIC,
};

inline const char *scale_names[] = {"Major", "Minor", "Chromatic"};

inline std::vector<Key> keys_from_scale_and_key(Scale scale, Key key) {
  switch (scale) {
  case Scale::MAJOR:
    return {
        shift_key(Key::C, static_cast<uint8_t>(key)),
        shift_key(Key::D, static_cast<uint8_t>(key)),
        shift_key(Key::E, static_cast<uint8_t>(key)),
        shift_key(Key::F, static_cast<uint8_t>(key)),
        shift_key(Key::G, static_cast<uint8_t>(key)),
        shift_key(Key::A, static_cast<uint8_t>(key)),
        shift_key(Key::B, static_cast<uint8_t>(key)),
    };

  case Scale::MINOR:
    return {
        shift_key(Key::C, static_cast<uint8_t>(key)),
        shift_key(Key::D, static_cast<uint8_t>(key)),
        shift_key(Key::D_SHARP, static_cast<uint8_t>(key)),
        shift_key(Key::F, static_cast<uint8_t>(key)),
        shift_key(Key::G, static_cast<uint8_t>(key)),
        shift_key(Key::G_SHARP, static_cast<uint8_t>(key)),
        shift_key(Key::A_SHARP, static_cast<uint8_t>(key)),
    };
  case Scale::CHROMATIC:
    return {Key::C,       Key::C_SHARP, Key::D,       Key::D_SHARP,
            Key::E,       Key::F,       Key::F_SHARP, Key::G,
            Key::G_SHARP, Key::A,       Key::A_SHARP, Key::B};
  }
};
