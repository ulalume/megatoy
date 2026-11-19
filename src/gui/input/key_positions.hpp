#pragma once

#include <SDL3/SDL.h>
#include <array>
#include <imgui.h>
#include <string_view>
#include <utility>

// imgui_impl_sdl3.cpp defines it without declaring it in its header.
ImGuiKey ImGui_ImplSDL3_KeyEventToImGuiKey(SDL_Keycode keycode,
                                           SDL_Scancode scancode);

namespace ui {

/// Whether a place on the keyboard is one of those whose keys type a
/// character: the letters, the digits over them and the signs around them.
constexpr bool holds_character(SDL_Scancode place) {
  return (place >= SDL_SCANCODE_A && place <= SDL_SCANCODE_0) ||
         (place >= SDL_SCANCODE_MINUS && place <= SDL_SCANCODE_SLASH) ||
         place == SDL_SCANCODE_NONUSBACKSLASH ||
         (place >= SDL_SCANCODE_INTERNATIONAL1 &&
          place <= SDL_SCANCODE_INTERNATIONAL3);
}

/// The place a browser calls `name`, as a key event's code does ("KeyA"), or
/// SDL_SCANCODE_UNKNOWN for one whose key types no character.
inline SDL_Scancode place_named(std::string_view name) {
  const auto offset = [](SDL_Scancode first, int steps) {
    return static_cast<SDL_Scancode>(static_cast<int>(first) + steps);
  };
  if (name.size() == 4 && name.starts_with("Key") && name[3] >= 'A' &&
      name[3] <= 'Z') {
    return offset(SDL_SCANCODE_A, name[3] - 'A');
  }
  if (name.size() == 6 && name.starts_with("Digit") && name[5] >= '0' &&
      name[5] <= '9') {
    return name[5] == '0' ? SDL_SCANCODE_0
                          : offset(SDL_SCANCODE_1, name[5] - '1');
  }
  constexpr std::pair<std::string_view, SDL_Scancode> signs[]{
      {"Minus", SDL_SCANCODE_MINUS},
      {"Equal", SDL_SCANCODE_EQUALS},
      {"BracketLeft", SDL_SCANCODE_LEFTBRACKET},
      {"BracketRight", SDL_SCANCODE_RIGHTBRACKET},
      {"Backslash", SDL_SCANCODE_BACKSLASH},
      {"Semicolon", SDL_SCANCODE_SEMICOLON},
      {"Quote", SDL_SCANCODE_APOSTROPHE},
      {"Backquote", SDL_SCANCODE_GRAVE},
      {"Comma", SDL_SCANCODE_COMMA},
      {"Period", SDL_SCANCODE_PERIOD},
      {"Slash", SDL_SCANCODE_SLASH},
      {"IntlBackslash", SDL_SCANCODE_NONUSBACKSLASH},
      {"IntlRo", SDL_SCANCODE_INTERNATIONAL1},
      {"IntlYen", SDL_SCANCODE_INTERNATIONAL3}};
  for (const auto &[sign, place] : signs) {
    if (name == sign) {
      return place;
    }
  }
  return SDL_SCANCODE_UNKNOWN;
}

/// Whether a place holds a letter's key or a digit's on a US keyboard, which
/// most layouts leave where they are, unlike the signs.
constexpr bool holds_letter_or_digit(SDL_Scancode place) {
  return place >= SDL_SCANCODE_A && place <= SDL_SCANCODE_0;
}

/// The key at a place on the keyboard, named as ImGui names it when it is
/// pressed. A place is given by its key on a US keyboard; on a French one,
/// the key at SDL_SCANCODE_A is ImGuiKey_Q.
inline ImGuiKey key_at(SDL_Scancode place) {
  return ImGui_ImplSDL3_KeyEventToImGuiKey(
      SDL_GetKeyFromScancode(place, SDL_KMOD_NONE, true), place);
}

/// The place of the key ImGui names `key` on the keyboard in use, or
/// SDL_SCANCODE_UNKNOWN where no key has that name.
inline SDL_Scancode place_of(ImGuiKey key) {
  if (key == ImGuiKey_None) {
    return SDL_SCANCODE_UNKNOWN;
  }
  for (int code = SDL_SCANCODE_UNKNOWN + 1; code < SDL_SCANCODE_COUNT; ++code) {
    const auto place = static_cast<SDL_Scancode>(code);
    if (key_at(place) == key) {
      return place;
    }
  }
  return SDL_SCANCODE_UNKNOWN;
}

/**
 * The character the key at each place types, which is the key's name.
 *
 * SDL has it from the layout in use on the desktop. In a browser SDL starts
 * from a US keyboard and comes to know a key's own character with the key's
 * press: what a press or the browser's layout map shows is taken in with
 * learn(), and until then at() is a guess, which knows() tells.
 */
class TypedCharacters {
public:
  SDL_Keycode at(SDL_Scancode place) {
    if (place <= SDL_SCANCODE_UNKNOWN || place >= SDL_SCANCODE_COUNT) {
      return SDLK_UNKNOWN;
    }
    see();
    return learned_[place] != SDLK_UNKNOWN
               ? learned_[place]
               : SDL_GetKeyFromScancode(place, SDL_KMOD_NONE, false);
  }

  /// Says that SDL starts without the keyboard's layout, as in a browser: a
  /// key's character is then known only once learn() has taken it in.
  void start_from_a_guess() {
    guessing_ = true;
    ++revision_;
  }

  /// Whether the character at() gives for `place` is the key's own rather
  /// than a US keyboard's, taken for want of it.
  bool knows(SDL_Scancode place) const {
    return !guessing_ || (place > SDL_SCANCODE_UNKNOWN &&
                          place < SDL_SCANCODE_COUNT && known_[place]);
  }

  /// Takes in that the key at `place` types `typed`. True when that was
  /// news.
  bool learn(SDL_Scancode place, SDL_Keycode typed) {
    const bool character =
        typed > ' ' && typed != SDLK_DELETE && typed <= 0x10FFFF;
    if (!holds_character(place) || !character) {
      return false;
    }
    const bool news = at(place) != typed || !knows(place);
    learned_[place] = typed;
    known_[place] = true;
    revision_ += news ? 1 : 0;
    return news;
  }

  /**
   * Takes in what SDL has come to know of a key with its press. A press with
   * nothing else held shows the key's own character, even where it is the
   * one a US keyboard has there; one with Shift held, say, shows it only if
   * SDL is left with another than it had. True when the press was news.
   */
  bool learn(const SDL_KeyboardEvent &press) {
    const SDL_Scancode place = press.scancode;
    if (!press.down || !holds_character(place)) {
      return false;
    }
    see();
    const SDL_Keycode typed =
        SDL_GetKeyFromScancode(place, SDL_KMOD_NONE, false);
    const bool changed = typed != seen_[place];
    const bool plain =
        (press.mod & ~(SDL_KMOD_NUM | SDL_KMOD_SCROLL)) == SDL_KMOD_NONE;
    if (!changed && (!plain || knows(place))) {
      return false;
    }
    seen_[place] = typed;
    if (changed) {
      learned_[place] = typed;
    }
    known_[place] = true;
    ++revision_;
    return true;
  }

  /// Starts over from what SDL has, as when the layout in use changes.
  void forget() {
    learned_.fill(SDLK_UNKNOWN);
    known_.fill(false);
    saw_ = false;
    ++revision_;
  }

  /// Goes up each time a key's character may have changed, for what is made
  /// from the keyboard's keys to be made again.
  unsigned revision() const { return revision_; }

private:
  void see() {
    if (saw_) {
      return;
    }
    for (int place = 0; place < SDL_SCANCODE_COUNT; ++place) {
      seen_[place] = SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(place),
                                            SDL_KMOD_NONE, false);
    }
    saw_ = true;
  }

  std::array<SDL_Keycode, SDL_SCANCODE_COUNT> learned_{};
  /// What SDL had for each place when its key was last pressed, or at the
  /// start.
  std::array<SDL_Keycode, SDL_SCANCODE_COUNT> seen_{};
  bool saw_ = false;
  /// The places whose own characters learn() has taken in.
  std::array<bool, SDL_SCANCODE_COUNT> known_{};
  bool guessing_ = false;
  unsigned revision_ = 0;
};

/// The characters of the keyboard in use.
inline TypedCharacters &typed_characters() {
  static TypedCharacters characters;
  return characters;
}

} // namespace ui
