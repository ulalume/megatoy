#pragma once

#include "gui/fonts/megatoy_icons.hpp"
#include "patches/patch_document.hpp"
#include <filesystem>
#include <string>
#include <string_view>

namespace ui {
constexpr char PATCH_EDITOR_TITLE[] = "Patch Editor";
constexpr char LIBRARY_TITLE[] = "Library";
constexpr char SOFT_KEYBOARD_TITLE[] = "Soft Keyboard";
constexpr char WAVEFORM_TITLE[] = "Waveform";
constexpr char PREFERENCES_TITLE[] = "Preferences";
constexpr char PATCH_LAB_TITLE[] = "Patch Lab";

constexpr char LIBRARY_ICON[] = ICON_MEGATOY_FOLDER_TREE;
constexpr char SOFT_KEYBOARD_ICON[] = ICON_MEGATOY_PIANO;
constexpr char WAVEFORM_ICON[] = ICON_MEGATOY_CHART_LINE;
constexpr char PREFERENCES_ICON[] = ICON_MEGATOY_GEAR;
constexpr char PATCH_LAB_ICON[] = ICON_MEGATOY_FLASK;

/// The part of a panel's window name that identifies it, which saved layouts
/// place it by and FindWindowByName finds it by.
inline std::string panel_window_id(const char *title) {
  return std::string("###") + title;
}

/// A panel's title with its icon in front, or the icon alone. The window
/// keeps `title` as its id, so saved layouts still place it.
inline std::string panel_window_title(const char *icon, const char *title,
                                      bool icon_only = false) {
  return std::string(icon) + (icon_only ? "" : std::string(" ") + title) +
         panel_window_id(title);
}

/// What a window's name keeps after "###", or the whole name.
inline std::string_view window_id(std::string_view name) {
  if (const auto at = name.find("###"); at != std::string_view::npos) {
    name.remove_prefix(at + 3);
  }
  return name;
}

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

/// An editor window's title. Unsaved changes are marked on its tab by
/// ImGuiWindowFlags_UnsavedDocument rather than in the title.
inline std::string patch_editor_window_title(const std::string &label,
                                             int slot) {
  return label + "###" + patch_editor_window_id(slot);
}

/// The title of the application's own window.
inline std::string app_window_title(const std::string &label,
                                    bool is_modified) {
  return label + (is_modified ? " * - megatoy" : " - megatoy");
}
} // namespace ui
