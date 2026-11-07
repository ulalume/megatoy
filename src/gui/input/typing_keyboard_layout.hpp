#pragma once

#include "gui/input/key_positions.hpp"
#include "preferences/typing_keyboard_layout.hpp"
#include <array>
#include <imgui.h>
#include <utility>
#include <vector>

namespace ui {

using TypingLayout = std::vector<ImGuiKey>;

/**
 * Where the typing keyboard's notes sit, from C up, by the keys that hold
 * those places on a US keyboard: the middle row for the white notes and the
 * row above for the black ones, so they make a piano's shape on any layout.
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

static_assert(typing_key_none == ImGuiKey_None);
static_assert(default_typing_octave_down_key == ImGuiKey_Comma);
static_assert(default_typing_octave_up_key == ImGuiKey_Period);

/// The keys at typing_key_places on the keyboard in use, ending in
/// ImGuiKey_None.
inline TypingLayout keyboard_typing_layout() {
  TypingLayout layout;
  layout.reserve(typing_key_places.size() + 1);
  for (const auto place : typing_key_places) {
    layout.push_back(key_at(place));
  }
  layout.push_back(ImGuiKey_None);
  return layout;
}

/// Octave down and up: the keys where a US keyboard has "," and ".".
inline std::pair<ImGuiKey, ImGuiKey> keyboard_octave_keys() {
  return {key_at(SDL_SCANCODE_COMMA), key_at(SDL_SCANCODE_PERIOD)};
}

inline constexpr TypingKeyboardLayout clamp_layout_pref(int value) {
  return static_cast<TypingKeyboardLayout>(value) ==
                 TypingKeyboardLayout::Custom
             ? TypingKeyboardLayout::Custom
             : TypingKeyboardLayout::Auto;
}

inline TypingLayout
layout_from_preferences(const TypingLayoutPreference &pref) {
  TypingLayout result;
  result.reserve(pref.size() + 1);
  for (auto key : pref) {
    result.push_back(static_cast<ImGuiKey>(key));
  }
  if (result.empty() || result.back() != ImGuiKey_None) {
    result.push_back(ImGuiKey_None);
  }
  return result;
}

/// A layout as the preferences keep it: the keys before its ImGuiKey_None.
inline void copy_layout_to_preferences(TypingLayoutPreference &pref,
                                       const TypingLayout &layout) {
  pref.clear();
  for (const auto key : layout) {
    if (key == ImGuiKey_None) {
      break;
    }
    pref.push_back(static_cast<int>(key));
  }
}

inline ImGuiKey key_from_preference(int stored_key, ImGuiKey fallback) {
  if (stored_key <= static_cast<int>(ImGuiKey_None) ||
      stored_key >= static_cast<int>(ImGuiKey_NamedKey_END)) {
    return fallback;
  }
  return static_cast<ImGuiKey>(stored_key);
}

} // namespace ui
