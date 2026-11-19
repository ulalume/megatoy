#pragma once

#include "core/types.hpp"
#include "input_state.hpp"
#include "typing_keyboard_layout.hpp"
#include "ym2612/note.hpp"
#include <functional>
#include <map>

namespace ui {

/// The note of each key of one keyboard: the scale's notes from its key note
/// in `selected_octave`, on every key for the chromatic scale and on the
/// white notes' keys for any other.
std::map<SDL_Scancode, ym2612::Note>
create_key_mappings(Scale scale, Key key, uint8_t selected_octave,
                    const TypingLayout &keyboard);

struct KeyboardTypingContext {
  InputState &input_state;
  SDL_Scancode octave_down_key = SDL_SCANCODE_COMMA;
  SDL_Scancode octave_up_key = SDL_SCANCODE_PERIOD;
  std::function<void(ym2612::Note, uint8_t)> key_on;
  std::function<void(ym2612::Note)> key_off;
  /// The upper keyboard's octave keys, where typing plays two.
  SDL_Scancode upper_octave_down_key = SDL_SCANCODE_UNKNOWN;
  SDL_Scancode upper_octave_up_key = SDL_SCANCODE_UNKNOWN;
  /// Whether the key at a place is held. Without it SDL is asked.
  std::function<bool(SDL_Scancode)> is_down;
};

/**
 * Keeps typing quiet at its next check: a key that goes down by then starts
 * no note and moves no octave, however long it stays down. Called each frame
 * a key is being asked for, since the key given is an answer and no note.
 */
void keep_typing_quiet();

/// Turns notes on and off as their keys go down and up, and moves the octaves
/// with their keys. A note two keys play sounds once, from the first key down
/// to the last one up.
void check_keyboard_typing(
    KeyboardTypingContext &context,
    const std::map<SDL_Scancode, ym2612::Note> &key_mappings);

} // namespace ui
