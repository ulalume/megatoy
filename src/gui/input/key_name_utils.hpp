#pragma once

#include <cctype>
#include <imgui.h>
#include <string>

namespace ui {

inline std::string short_key_name(ImGuiKey key) {
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
