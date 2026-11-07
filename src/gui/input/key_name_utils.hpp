#pragma once

#include "gui/input/key_positions.hpp"
#include <cctype>
#include <imgui.h>
#include <map>
#include <string>

namespace ui {

/**
 * The character each key types on the keyboard layout in use. ImGui names a
 * symbol key by where it sits on a US keyboard, so on another layout -- a
 * Japanese one, say -- the key that types "[" is ImGuiKey_RightBracket. Only
 * printable ASCII, which the font has.
 */
inline const std::map<ImGuiKey, std::string> &layout_key_names() {
  static const auto names = [] {
    std::map<ImGuiKey, std::string> names;
    for (int code = 0; code < SDL_SCANCODE_COUNT; ++code) {
      const auto scancode = static_cast<SDL_Scancode>(code);
      const SDL_Keycode keycode =
          SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
      if (keycode <= ' ' || keycode >= 0x7F) {
        continue;
      }
      const ImGuiKey key = ImGui_ImplSDL3_KeyEventToImGuiKey(keycode, scancode);
      if (key != ImGuiKey_None) {
        names.emplace(key, std::string(1, static_cast<char>(std::toupper(
                                              static_cast<int>(keycode)))));
      }
    }
    return names;
  }();
  return names;
}

inline std::string short_key_name(ImGuiKey key) {
  const auto &on_layout = layout_key_names();
  if (const auto found = on_layout.find(key); found != on_layout.end()) {
    return found->second;
  }
  switch (key) {
  case ImGuiKey_Semicolon:
    return ";";
  case ImGuiKey_Comma:
    return ",";
  case ImGuiKey_Period:
    return ".";
  case ImGuiKey_Slash:
    return "/";
  case ImGuiKey_Apostrophe:
    return "'";
  case ImGuiKey_LeftBracket:
    return "[";
  case ImGuiKey_RightBracket:
    return "]";
  case ImGuiKey_Backslash:
    return "\\";
  case ImGuiKey_Minus:
    return "-";
  case ImGuiKey_Equal:
    return "=";
  case ImGuiKey_GraveAccent:
    return "`";
  case ImGuiKey_Space:
    return "Sp";
  default:
    break;
  }

  const char *key_name = ImGui::GetKeyName(key);
  if (key_name == nullptr || key_name[0] == '\0') {
    return "??";
  }

  std::string name = key_name;
  if (name.size() <= 2) {
    return name;
  }

  std::string shortened{name.substr(0, 2)};
  shortened[0] = static_cast<char>(std::toupper(shortened[0]));
  shortened[1] = static_cast<char>(std::tolower(shortened[1]));
  return shortened;
}

} // namespace ui
