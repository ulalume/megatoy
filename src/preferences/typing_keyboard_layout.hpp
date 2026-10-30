#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace ui {

enum class TypingKeyboardLayout : int {
  Qwerty = 0,
  Azerty = 1,
  Custom = 2,
};

inline constexpr std::array<const char *, 3> typing_keyboard_layout_names{
    "QWERTY", "AZERTY", "Custom"};

inline constexpr std::size_t typing_layout_default_slot_count = 17;
inline constexpr std::size_t typing_layout_builtin_entry_count =
    typing_layout_default_slot_count + 1;
inline constexpr std::size_t typing_layout_min_custom_slots = 12;
inline constexpr std::size_t typing_layout_max_custom_slots = 32;

using TypingLayoutPreference = std::vector<int>;

// Key integers are part of the preferences file format. The GUI checks these
// defaults against its key enum so settings keep the same meaning.
inline constexpr int typing_key_none = 0;
inline constexpr int default_typing_octave_down_key = 597;
inline constexpr int default_typing_octave_up_key = 599;
inline constexpr std::array<int, typing_layout_default_slot_count>
    default_typing_layout_keys{546, 568, 564, 550, 549, 551, 565, 552, 570,
                               553, 566, 555, 556, 560, 557, 561, 601};

} // namespace ui
