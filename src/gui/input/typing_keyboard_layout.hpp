#pragma once

#include <array>
#include <imgui.h>

namespace ui {

enum class TypingKeyboardLayout : int {
  Qwerty = 0,
  Azerty = 1,
};

inline constexpr std::array<const char *, 2> typing_keyboard_layout_names{
    "QWERTY", "AZERTY"};

inline constexpr std::array<ImGuiKey, 18> qwerty_typing_layout_keys{
    ImGuiKey_A, ImGuiKey_W,         ImGuiKey_S,   ImGuiKey_E, ImGuiKey_D,
    ImGuiKey_F, ImGuiKey_T,         ImGuiKey_G,   ImGuiKey_Y, ImGuiKey_H,
    ImGuiKey_U, ImGuiKey_J,         ImGuiKey_K,   ImGuiKey_O, ImGuiKey_L,
    ImGuiKey_P, ImGuiKey_Semicolon, ImGuiKey_None};

inline constexpr std::array<ImGuiKey, 18> azerty_typing_layout_keys{
    ImGuiKey_Q, ImGuiKey_Z, ImGuiKey_S, ImGuiKey_E, ImGuiKey_D, ImGuiKey_F,
    ImGuiKey_T, ImGuiKey_G, ImGuiKey_Y, ImGuiKey_H, ImGuiKey_U, ImGuiKey_J,
    ImGuiKey_K, ImGuiKey_O, ImGuiKey_L, ImGuiKey_P, ImGuiKey_M, ImGuiKey_None};

inline constexpr const std::array<ImGuiKey, 18> &
typing_layout_keys(TypingKeyboardLayout layout) {
  return layout == TypingKeyboardLayout::Azerty ? azerty_typing_layout_keys
                                                : qwerty_typing_layout_keys;
}

inline constexpr TypingKeyboardLayout clamp_layout_pref(int value) {
  return value == static_cast<int>(TypingKeyboardLayout::Azerty)
             ? TypingKeyboardLayout::Azerty
             : TypingKeyboardLayout::Qwerty;
}

} // namespace ui
