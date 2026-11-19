#pragma once

#include "gui/input/key_positions.hpp"
#include "preferences/preferences_data.hpp"
#include "preferences/typing_keyboard_layout.hpp"
#include <algorithm>
#include <array>
#include <utility>
#include <vector>

namespace ui {

/**
 * A keyboard that typing plays: the places of its keys from C up, a semitone
 * apart, ending at its end or at its first SDL_SCANCODE_UNKNOWN. A place is
 * where a key sits, named by the key a US keyboard has there, so a layout
 * makes the same shape under the fingers whatever the keys type.
 */
using TypingLayout = std::vector<SDL_Scancode>;

/// Octave down and up.
using OctavePlaces = std::pair<SDL_Scancode, SDL_Scancode>;

/**
 * Where the typing keyboard's notes sit: the middle row for the white notes
 * and the row above for the black ones, as a piano has them.
 */
inline constexpr std::array<SDL_Scancode, 20> typing_key_places{
    SDL_SCANCODE_A,
    SDL_SCANCODE_W,
    SDL_SCANCODE_S,
    SDL_SCANCODE_E,
    SDL_SCANCODE_D,
    SDL_SCANCODE_F,
    SDL_SCANCODE_T,
    SDL_SCANCODE_G,
    SDL_SCANCODE_Y,
    SDL_SCANCODE_H,
    SDL_SCANCODE_U,
    SDL_SCANCODE_J,
    SDL_SCANCODE_K,
    SDL_SCANCODE_O,
    SDL_SCANCODE_L,
    SDL_SCANCODE_P,
    SDL_SCANCODE_SEMICOLON,
    SDL_SCANCODE_APOSTROPHE,
    SDL_SCANCODE_RIGHTBRACKET,
    SDL_SCANCODE_BACKSLASH};

/// Its octave down and up: where a US keyboard has "," and ".".
inline constexpr OctavePlaces typing_octave_places{SDL_SCANCODE_COMMA,
                                                   SDL_SCANCODE_PERIOD};

/**
 * The two keyboards' places. The lower one has the bottom row for its white
 * notes and the middle row for its black ones; the upper one has the row over
 * that and the digits.
 */
inline constexpr std::array<SDL_Scancode, 17> lower_keyboard_places{
    SDL_SCANCODE_Z,         SDL_SCANCODE_S,    SDL_SCANCODE_X,
    SDL_SCANCODE_D,         SDL_SCANCODE_C,    SDL_SCANCODE_V,
    SDL_SCANCODE_G,         SDL_SCANCODE_B,    SDL_SCANCODE_H,
    SDL_SCANCODE_N,         SDL_SCANCODE_J,    SDL_SCANCODE_M,
    SDL_SCANCODE_COMMA,     SDL_SCANCODE_L,    SDL_SCANCODE_PERIOD,
    SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_SLASH};
inline constexpr std::array<SDL_Scancode, 20> upper_keyboard_places{
    SDL_SCANCODE_Q,      SDL_SCANCODE_2,           SDL_SCANCODE_W,
    SDL_SCANCODE_3,      SDL_SCANCODE_E,           SDL_SCANCODE_R,
    SDL_SCANCODE_5,      SDL_SCANCODE_T,           SDL_SCANCODE_6,
    SDL_SCANCODE_Y,      SDL_SCANCODE_7,           SDL_SCANCODE_U,
    SDL_SCANCODE_I,      SDL_SCANCODE_9,           SDL_SCANCODE_O,
    SDL_SCANCODE_0,      SDL_SCANCODE_P,           SDL_SCANCODE_LEFTBRACKET,
    SDL_SCANCODE_EQUALS, SDL_SCANCODE_RIGHTBRACKET};

/// Each one's octave down and up: the ends of its row of black notes, where
/// a US keyboard has "A" and "'", and "1" and "-".
inline constexpr OctavePlaces lower_keyboard_octave_places{
    SDL_SCANCODE_A, SDL_SCANCODE_APOSTROPHE};
inline constexpr OctavePlaces upper_keyboard_octave_places{SDL_SCANCODE_1,
                                                           SDL_SCANCODE_MINUS};

template <std::size_t Count>
TypingLayout layout_of(const std::array<SDL_Scancode, Count> &places) {
  return TypingLayout(places.begin(), places.end());
}

/// A keyboard typing plays: the places of its keys and of its octave keys.
struct TypingKeys {
  TypingLayout places;
  OctavePlaces octave;

  friend bool operator==(const TypingKeys &, const TypingKeys &) = default;
};

inline constexpr TypingKeyboardLayout clamp_layout_pref(int value) {
  return static_cast<TypingKeyboardLayout>(value) ==
                 TypingKeyboardLayout::TwoKeyboards
             ? TypingKeyboardLayout::TwoKeyboards
             : TypingKeyboardLayout::OneKeyboard;
}

/// The keyboards of `layout` with the keys it gives them, the lowest first.
inline std::vector<TypingKeys>
standard_typing_keys(TypingKeyboardLayout layout) {
  if (layout == TypingKeyboardLayout::TwoKeyboards) {
    return {{layout_of(lower_keyboard_places), lower_keyboard_octave_places},
            {layout_of(upper_keyboard_places), upper_keyboard_octave_places}};
  }
  return {{layout_of(typing_key_places), typing_octave_places}};
}

/// A place as the preferences keep it, or `fallback` for a number that is no
/// key's.
inline SDL_Scancode place_from_preference(int stored, SDL_Scancode fallback) {
  if (stored <= SDL_SCANCODE_UNKNOWN || stored >= SDL_SCANCODE_COUNT) {
    return fallback;
  }
  return static_cast<SDL_Scancode>(stored);
}

inline TypingLayout
layout_from_preferences(const TypingLayoutPreference &pref) {
  TypingLayout result;
  result.reserve(pref.size());
  for (const auto place : pref) {
    result.push_back(place_from_preference(place, SDL_SCANCODE_UNKNOWN));
  }
  return result;
}

/// The user's keys for the keyboards of `layout`, the lowest first.
inline std::vector<TypingKeysPreference *>
custom_typing_keys(UIPreferences &prefs, TypingKeyboardLayout layout) {
  if (layout == TypingKeyboardLayout::TwoKeyboards) {
    return {&prefs.custom_typing_lower_keys, &prefs.custom_typing_upper_keys};
  }
  return {&prefs.custom_typing_keys};
}

/**
 * Makes `keys` ones a keyboard can have. Without places it takes
 * `standard`'s; it gets no fewer and no more than a custom keyboard may have;
 * and an octave key without a place takes `standard`'s.
 */
inline void fill_custom_typing_keys(TypingKeysPreference &keys,
                                    const TypingKeys &standard) {
  if (keys.places.empty()) {
    keys.places.assign(standard.places.begin(), standard.places.end());
  }
  keys.places.resize(std::clamp(keys.places.size(),
                                typing_layout_min_custom_slots,
                                typing_layout_max_custom_slots),
                     typing_place_none);
  keys.octave_down =
      place_from_preference(keys.octave_down, standard.octave.first);
  keys.octave_up =
      place_from_preference(keys.octave_up, standard.octave.second);
}

/**
 * Turns a custom layout an older file kept, by what its keys type, into the
 * places those keys have on the keyboard in use. A key the keyboard has not
 * got leaves its note without one.
 */
inline void adopt_legacy_typing_keys(UIPreferences &prefs) {
  const auto legacy =
      std::exchange(prefs.legacy_custom_typing_keys, std::nullopt);
  if (!legacy) {
    return;
  }
  const auto place = [](int key) {
    const bool named = key > static_cast<int>(ImGuiKey_None) &&
                       key < static_cast<int>(ImGuiKey_NamedKey_END);
    return static_cast<int>(named ? place_of(static_cast<ImGuiKey>(key))
                                  : SDL_SCANCODE_UNKNOWN);
  };
  auto &keys = prefs.custom_typing_keys;
  keys.places.clear();
  for (const auto key : legacy->layout) {
    keys.places.push_back(place(key));
  }
  keys.octave_down = place(legacy->octave_down);
  keys.octave_up = place(legacy->octave_up);
}

/**
 * The keyboards typing plays under the preferences, the lowest first: the
 * layout's, with the user's keys in place of its own where the preferences
 * say so. The preferences are put in order on the way.
 */
inline std::vector<TypingKeys> typing_keyboards(UIPreferences &prefs) {
  adopt_legacy_typing_keys(prefs);
  const auto layout = clamp_layout_pref(prefs.midi_keyboard_layout);
  prefs.midi_keyboard_layout = static_cast<int>(layout);
  auto keyboards = standard_typing_keys(layout);
  if (!prefs.midi_keyboard_custom_keys) {
    return keyboards;
  }
  const auto custom = custom_typing_keys(prefs, layout);
  for (std::size_t i = 0; i < keyboards.size(); ++i) {
    fill_custom_typing_keys(*custom[i], keyboards[i]);
    keyboards[i] = {layout_from_preferences(custom[i]->places),
                    {static_cast<SDL_Scancode>(custom[i]->octave_down),
                     static_cast<SDL_Scancode>(custom[i]->octave_up)}};
  }
  return keyboards;
}

} // namespace ui
