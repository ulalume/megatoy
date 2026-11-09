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

/// The keyboard's scales. Their values are what the preferences keep, so a
/// new one goes at the end; the lists take kScaleInfos' order.
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

  YO,
  EGYPTIAN,
  MAN_GONG,
  HIRAJOSHI,
  IN_SEN,
  IWATO,
  LYDIAN_AUGMENTED,
  LYDIAN_DOMINANT,
  MELODIC_MINOR,
  MIXOLYDIAN_FLAT_6,
  DORIAN_FLAT_2,
  LOCRIAN_NATURAL_2,
  ALTERED,
  BEBOP_DOMINANT,
  BEBOP_MAJOR,
  HARMONIC_MAJOR,
  DOUBLE_HARMONIC_MAJOR,
  PHRYGIAN_DOMINANT,
  WHOLE_HALF_DIMINISHED,
  HALF_WHOLE_DIMINISHED,
};

inline constexpr int kScaleCount =
    static_cast<int>(Scale::HALF_WHOLE_DIMINISHED) + 1;

/// The families the scales are listed in, from smooth to tense.
inline constexpr const char *kScaleGroups[] = {
    "Pentatonic",    "Modes",    "Japanese",  "Melodic Minor",
    "Blues & Bebop", "Harmonic", "Symmetric",
};

struct ScaleInfo {
  Scale scale;
  const char *name;
  /// The mode's other name, or none.
  const char *alias;
  /// Its family in kScaleGroups, or -1 for Chromatic, which is every key.
  int group;
  /// Semitones above the key.
  std::vector<uint8_t> steps;
};

/// Every scale, in the order the lists show them: by family, and in each
/// from bright to dark.
inline const std::vector<ScaleInfo> &scale_infos() {
  static const std::vector<ScaleInfo> infos = {
      {Scale::CHROMATIC, "Chromatic", nullptr, -1, {}},
      {Scale::MAJAR_PENTATONIC,
       "Major Pentatonic",
       nullptr,
       0,
       {0, 2, 4, 7, 9}},
      {Scale::YO, "Yo", nullptr, 0, {0, 2, 5, 7, 9}},
      {Scale::EGYPTIAN, "Egyptian", nullptr, 0, {0, 2, 5, 7, 10}},
      {Scale::MINOR_PENTATONIC,
       "Minor Pentatonic",
       nullptr,
       0,
       {0, 3, 5, 7, 10}},
      {Scale::MAN_GONG, "Man Gong", nullptr, 0, {0, 3, 5, 8, 10}},
      {Scale::LYDIAN, "Lydian", nullptr, 1, {0, 2, 4, 6, 7, 9, 11}},
      {Scale::MAJOR, "Major", "Ionian", 1, {0, 2, 4, 5, 7, 9, 11}},
      {Scale::MIXOLYDIAN, "Mixolydian", nullptr, 1, {0, 2, 4, 5, 7, 9, 10}},
      {Scale::DORIAN, "Dorian", nullptr, 1, {0, 2, 3, 5, 7, 9, 10}},
      {Scale::MINOR, "Minor", "Aeolian", 1, {0, 2, 3, 5, 7, 8, 10}},
      {Scale::PHRYGIAN, "Phrygian", nullptr, 1, {0, 1, 3, 5, 7, 8, 10}},
      {Scale::LOCRIAN, "Locrian", nullptr, 1, {0, 1, 3, 5, 6, 8, 10}},
      {Scale::RYUKYU, "Ryukyu", nullptr, 2, {0, 4, 5, 7, 11}},
      {Scale::HIRAJOSHI, "Hirajoshi", nullptr, 2, {0, 2, 3, 7, 8}},
      {Scale::IN_SEN, "In-sen", nullptr, 2, {0, 1, 5, 7, 10}},
      {Scale::IWATO, "Iwato", nullptr, 2, {0, 1, 5, 6, 10}},
      {Scale::LYDIAN_AUGMENTED,
       "Lydian Augmented",
       nullptr,
       3,
       {0, 2, 4, 6, 8, 9, 11}},
      {Scale::LYDIAN_DOMINANT,
       "Lydian Dominant",
       nullptr,
       3,
       {0, 2, 4, 6, 7, 9, 10}},
      {Scale::MELODIC_MINOR,
       "Melodic Minor",
       nullptr,
       3,
       {0, 2, 3, 5, 7, 9, 11}},
      {Scale::MIXOLYDIAN_FLAT_6,
       "Mixolydian b6",
       nullptr,
       3,
       {0, 2, 4, 5, 7, 8, 10}},
      {Scale::DORIAN_FLAT_2, "Dorian b2", nullptr, 3, {0, 1, 3, 5, 7, 9, 10}},
      {Scale::LOCRIAN_NATURAL_2,
       "Locrian #2",
       nullptr,
       3,
       {0, 2, 3, 5, 6, 8, 10}},
      {Scale::ALTERED, "Altered", nullptr, 3, {0, 1, 3, 4, 6, 8, 10}},
      {Scale::BEBOP_DOMINANT,
       "Bebop Dominant",
       nullptr,
       4,
       {0, 2, 4, 5, 7, 9, 10, 11}},
      {Scale::BEBOP_MAJOR,
       "Bebop Major",
       nullptr,
       4,
       {0, 2, 4, 5, 7, 8, 9, 11}},
      {Scale::MAJAR_BLUES, "Major Blues", nullptr, 4, {0, 2, 3, 4, 7, 9}},
      {Scale::MINOR_BLUES, "Minor Blues", nullptr, 4, {0, 3, 5, 6, 7, 10}},
      {Scale::HARMONIC_MAJOR,
       "Harmonic Major",
       nullptr,
       5,
       {0, 2, 4, 5, 7, 8, 11}},
      {Scale::HARMONIC_MINOR,
       "Harmonic Minor",
       nullptr,
       5,
       {0, 2, 3, 5, 7, 8, 11}},
      {Scale::DOUBLE_HARMONIC_MAJOR,
       "Double Harmonic Major",
       nullptr,
       5,
       {0, 1, 4, 5, 7, 8, 11}},
      {Scale::PHRYGIAN_DOMINANT,
       "Phrygian Dominant",
       nullptr,
       5,
       {0, 1, 4, 5, 7, 8, 10}},
      {Scale::WHOLE_TONE, "Whole Tone", nullptr, 6, {0, 2, 4, 6, 8, 10}},
      {Scale::WHOLE_HALF_DIMINISHED,
       "Whole-Half Diminished",
       nullptr,
       6,
       {0, 2, 3, 5, 6, 8, 9, 11}},
      {Scale::HALF_WHOLE_DIMINISHED,
       "Half-Whole Diminished",
       nullptr,
       6,
       {0, 1, 3, 4, 6, 7, 9, 10}},
  };
  return infos;
}

/// `scale`'s entry; Chromatic's for a value outside the enum.
inline const ScaleInfo &scale_info(Scale scale) {
  for (const ScaleInfo &info : scale_infos()) {
    if (info.scale == scale) {
      return info;
    }
  }
  return scale_infos().front();
}

inline const char *scale_name(Scale scale) { return scale_info(scale).name; }

inline std::vector<Key> keys_from_scale_and_key(Scale scale, Key key) {
  // Chromatic is every key whatever the selected one, and so is a value
  // outside the enum.
  const ScaleInfo &info = scale_info(scale);
  if (info.steps.empty()) {
    return {all_keys.begin(), all_keys.end()};
  }
  std::vector<Key> keys;
  keys.reserve(info.steps.size());
  for (const auto offset : info.steps) {
    keys.push_back(
        shift_key(static_cast<Key>(offset), static_cast<uint8_t>(key)));
  }
  return keys;
}
