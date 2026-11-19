#pragma once

#include "core/types.hpp"
#include "ym2612/note.hpp"
#include <cstdint>
#include <map>

struct MidiKeyboardSettings {
  Key key = Key::C;
  Scale scale = Scale::CHROMATIC;
};

struct InputState {
  uint8_t keyboard_typing_octave = 4;
  /// The upper keyboard's, where typing plays two.
  uint8_t keyboard_typing_upper_octave = 5;
  MidiKeyboardSettings midi_keyboard_settings;

  std::map<int, ym2612::Note> active_keyboard_notes;
};
