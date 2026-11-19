#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace ui {

/// Stored as an int. 1 was AZERTY, which OneKeyboard took over with QWERTY.
enum class TypingKeyboardLayout : int {
  /// One keyboard on the middle row and the row over it: see
  /// typing_key_places.
  OneKeyboard = 0,
  /// Two keyboards, a lower and an upper, each with an octave of its own: see
  /// standard_typing_keys().
  TwoKeyboards = 3,
};

/// The layout older files kept for one keyboard with keys of the user's own,
/// which is now OneKeyboard with custom keys.
inline constexpr int typing_layout_once_custom = 2;

inline constexpr std::size_t typing_layout_min_custom_slots = 12;
inline constexpr std::size_t typing_layout_max_custom_slots = 32;

using TypingLayoutPreference = std::vector<int>;

// A key is kept by its place on the keyboard, as the number USB gives the key
// there, which SDL's scancodes are: 4 is where a US keyboard has "A". The
// numbers are part of the preferences file format.
inline constexpr int typing_place_none = 0;

/**
 * A keyboard's keys as the user has put them: the places of its notes from C
 * up, and of its octave keys. Without places it has the keys of its layout,
 * and an octave key without a place is the layout's.
 */
struct TypingKeysPreference {
  TypingLayoutPreference places;
  int octave_down = typing_place_none;
  int octave_up = typing_place_none;

  friend bool operator==(const TypingKeysPreference &,
                         const TypingKeysPreference &) = default;
};

// Older files kept a key by the GUI's number for what it types, 546 being
// "A", and so kept a US keyboard's layout until its custom layout was changed.
inline constexpr int typing_key_none = 0;
inline constexpr std::size_t typing_layout_default_slot_count = 17;
inline constexpr std::size_t typing_layout_builtin_entry_count =
    typing_layout_default_slot_count + 1;
inline constexpr int default_typing_octave_down_key = 597;
inline constexpr int default_typing_octave_up_key = 599;
inline constexpr std::array<int, typing_layout_default_slot_count>
    default_typing_layout_keys{546, 568, 564, 550, 549, 551, 565, 552, 570,
                               553, 566, 555, 556, 560, 557, 561, 601};

/// A custom layout as an older file kept it, for the GUI to turn into places
/// on the keyboard in use.
struct LegacyTypingKeys {
  TypingLayoutPreference layout;
  int octave_down = default_typing_octave_down_key;
  int octave_up = default_typing_octave_up_key;

  /// Whether it is the layout such a file held before the user changed it.
  bool untouched() const {
    return octave_down == default_typing_octave_down_key &&
           octave_up == default_typing_octave_up_key &&
           layout.size() == default_typing_layout_keys.size() &&
           std::equal(layout.begin(), layout.end(),
                      default_typing_layout_keys.begin());
  }

  friend bool operator==(const LegacyTypingKeys &,
                         const LegacyTypingKeys &) = default;
};

} // namespace ui
