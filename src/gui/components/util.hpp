#pragma once

#include "core/types.hpp"

namespace ui {

constexpr uint8_t midi_note_start = 21;
constexpr uint8_t midi_note_end = 107;
constexpr uint8_t midi_note_length = midi_note_end - midi_note_start + 1;

inline constexpr bool is_white_key(Key key) {
  switch (key) {
  case Key::C:
  case Key::D:
  case Key::E:
  case Key::F:
  case Key::G:
  case Key::A:
  case Key::B:
    return true;
  default:
    return false;
  }
}

} // namespace ui
