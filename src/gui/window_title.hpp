#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace ui {
constexpr char PATCH_EDITOR_TITLE[] = "Patch Editor";
constexpr char PATCH_BROWSER_TITLE[] = "Patch Browser";
constexpr char SOFT_KEYBOARD_TITLE[] = "Soft Keyboard";
constexpr char MML_CONSOLE_TITLE[] = "MML Console";
constexpr char WAVEFORM_TITLE[] = "Waveform";
constexpr char PREFERENCES_TITLE[] = "Preferences";
constexpr char PATCH_LAB_TITLE[] = "Patch Lab";

inline std::string patch_identity(std::string_view current_patch_path) {
  if (current_patch_path.empty()) {
    return "Untitled";
  }
  return std::filesystem::path(current_patch_path).stem().string();
}

inline std::string
patch_editor_window_title(const char *stable_id,
                          std::string_view current_patch_path,
                          bool is_modified) {
  return patch_identity(current_patch_path) + (is_modified ? " *###" : "###") +
         stable_id;
}
} // namespace ui
