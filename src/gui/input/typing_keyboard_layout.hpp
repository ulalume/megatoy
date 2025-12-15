#pragma once

#include <array>
#include <cstddef>
#include <imgui.h>
#include <utility>
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

using BuiltinTypingLayoutArray =
    std::array<ImGuiKey, typing_layout_builtin_entry_count>;
using TypingLayout = std::vector<ImGuiKey>;
using TypingLayoutPreference = std::vector<int>;

inline constexpr BuiltinTypingLayoutArray qwerty_typing_layout_keys{
    ImGuiKey_A, ImGuiKey_W,         ImGuiKey_S,   ImGuiKey_E, ImGuiKey_D,
    ImGuiKey_F, ImGuiKey_T,         ImGuiKey_G,   ImGuiKey_Y, ImGuiKey_H,
    ImGuiKey_U, ImGuiKey_J,         ImGuiKey_K,   ImGuiKey_O, ImGuiKey_L,
    ImGuiKey_P, ImGuiKey_Semicolon, ImGuiKey_None};

inline constexpr BuiltinTypingLayoutArray azerty_typing_layout_keys{
    ImGuiKey_Q, ImGuiKey_Z, ImGuiKey_S, ImGuiKey_E, ImGuiKey_D, ImGuiKey_F,
    ImGuiKey_T, ImGuiKey_G, ImGuiKey_Y, ImGuiKey_H, ImGuiKey_U, ImGuiKey_J,
    ImGuiKey_K, ImGuiKey_O, ImGuiKey_L, ImGuiKey_P, ImGuiKey_M, ImGuiKey_None};

inline TypingLayout builtin_typing_layout(TypingKeyboardLayout layout) {
  TypingLayout layout_vec;
  const auto &src = layout == TypingKeyboardLayout::Azerty
                        ? azerty_typing_layout_keys
                        : qwerty_typing_layout_keys;
  layout_vec.reserve(src.size());
  for (auto key : src) {
    layout_vec.push_back(key);
  }
  return layout_vec;
}

inline constexpr TypingKeyboardLayout clamp_layout_pref(int value) {
  switch (static_cast<TypingKeyboardLayout>(value)) {
  case TypingKeyboardLayout::Azerty:
    return TypingKeyboardLayout::Azerty;
  case TypingKeyboardLayout::Custom:
    return TypingKeyboardLayout::Custom;
  case TypingKeyboardLayout::Qwerty:
  default:
    return TypingKeyboardLayout::Qwerty;
  }
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

inline void
copy_builtin_to_preferences(TypingLayoutPreference &pref,
                            const BuiltinTypingLayoutArray &builtin) {
  pref.clear();
  pref.reserve(typing_layout_default_slot_count);
  for (auto key : builtin) {
    if (key == ImGuiKey_None)
      break;
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

inline std::pair<ImGuiKey, ImGuiKey>
default_octave_keys(TypingKeyboardLayout layout) {
  switch (layout) {
  case TypingKeyboardLayout::Azerty:
    return {ImGuiKey_Comma, ImGuiKey_Semicolon};
  case TypingKeyboardLayout::Custom:
    return {ImGuiKey_Comma, ImGuiKey_Period};
  case TypingKeyboardLayout::Qwerty:
  default:
    return {ImGuiKey_Comma, ImGuiKey_Period};
  }
}

} // namespace ui
