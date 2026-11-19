#pragma once

#include "gui/input/key_positions.hpp"
#include <cctype>
#include <string>

namespace ui {

/**
 * The name of the key at a place, to show on a small key: the character it
 * types on the keyboard layout in use, so the key right of "P" on a Japanese
 * keyboard is "@". Only printable ASCII, which the font has; another
 * character's key has the name a US keyboard gives its place.
 *
 * Where the layout is not known yet a letter's key or a digit's has that
 * name too, as most layouts leave those where they are, and a sign's key has
 * none: it is empty until the key's character is learned.
 */
inline std::string
place_name(SDL_Scancode place,
           TypedCharacters &characters = typed_characters()) {
  if (place <= SDL_SCANCODE_UNKNOWN || place >= SDL_SCANCODE_COUNT) {
    return {};
  }
  if (place == SDL_SCANCODE_SPACE) {
    return "Sp";
  }
  if (holds_character(place)) {
    if (!characters.knows(place) && !holds_letter_or_digit(place)) {
      return {};
    }
    const SDL_Keycode typed = characters.at(place);
    if (typed > ' ' && typed < 0x7F) {
      return std::string(
          1, static_cast<char>(std::toupper(static_cast<int>(typed))));
    }
  }

  // "Left Shift", "F1": the first two letters, or all of a shorter name.
  std::string name = SDL_GetScancodeName(place);
  if (name.empty()) {
    return "??";
  }
  if (name.size() > 2) {
    name.resize(2);
    name[0] =
        static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    name[1] =
        static_cast<char>(std::tolower(static_cast<unsigned char>(name[1])));
  }
  return name;
}

} // namespace ui
