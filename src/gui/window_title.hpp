#pragma once

#include "patches/patch_document.hpp"
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

using patches::document_label;
using patches::patch_identity;

/// The stable id of the editor window in `slot`: the first one keeps the
/// single editor's id, so saved layouts still place it.
inline std::string patch_editor_window_id(int slot) {
  std::string id = PATCH_EDITOR_TITLE;
  if (slot > 1) {
    id += " " + std::to_string(slot);
  }
  return id;
}

inline std::string patch_editor_window_title(const std::string &label, int slot,
                                             bool is_modified) {
  return label + (is_modified ? " *###" : "###") + patch_editor_window_id(slot);
}

/// The title of the application's own window.
inline std::string app_window_title(const std::string &label,
                                    bool is_modified) {
  return label + (is_modified ? " * - megatoy" : " - megatoy");
}
} // namespace ui
