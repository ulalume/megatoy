#pragma once

#include "core/types.hpp"
#include "gui/input/typing_keyboard_layout.hpp"
#include "gui/styles/theme.hpp"
#include <filesystem>
#include <string>
#include <vector>

struct UIPreferences {
  bool show_patch_editor = true;
  bool show_audio_controls = true;
  bool show_midi_keyboard = true;
  bool show_patch_selector = true;
  bool show_mml_console = true;
  bool show_preferences = false;
  bool show_waveform = true;
  bool show_patch_lab = true;
  bool use_velocity = true;
  bool steal_oldest_note_when_full = true;
  std::string patch_search_query;

  // Patch selector view settings
  int patch_sort_column =
      0; // 0=Name, 1=Category, 2=StarRating, 3=Format, 4=Path
  int patch_sort_order = 0; // 0=Ascending, 1=Descending
  std::string metadata_search_query;
  int metadata_star_filter = 0;

  int midi_keyboard_scale = static_cast<int>(Scale::CHROMATIC);
  int midi_keyboard_key = static_cast<int>(Key::C);
  int midi_keyboard_typing_octave = 4;
  int midi_keyboard_layout = static_cast<int>(ui::TypingKeyboardLayout::Qwerty);
  ui::TypingLayoutPreference custom_typing_layout_keys = []() {
    ui::TypingLayoutPreference pref;
    ui::copy_builtin_to_preferences(pref, ui::qwerty_typing_layout_keys);
    return pref;
  }();
  int custom_typing_octave_down_key = static_cast<int>(ImGuiKey_Comma);
  int custom_typing_octave_up_key = static_cast<int>(ImGuiKey_Period);

  friend bool operator==(const UIPreferences &lhs, const UIPreferences &rhs) {
    return lhs.show_patch_editor == rhs.show_patch_editor &&
           lhs.show_audio_controls == rhs.show_audio_controls &&
           lhs.show_midi_keyboard == rhs.show_midi_keyboard &&
           lhs.show_patch_selector == rhs.show_patch_selector &&
           lhs.show_mml_console == rhs.show_mml_console &&
           lhs.show_preferences == rhs.show_preferences &&
           lhs.show_waveform == rhs.show_waveform &&
           lhs.show_patch_lab == rhs.show_patch_lab &&
           lhs.use_velocity == rhs.use_velocity &&
           lhs.steal_oldest_note_when_full == rhs.steal_oldest_note_when_full &&
           lhs.patch_search_query == rhs.patch_search_query &&
           lhs.patch_sort_column == rhs.patch_sort_column &&
           lhs.patch_sort_order == rhs.patch_sort_order &&
           lhs.metadata_search_query == rhs.metadata_search_query &&
           lhs.metadata_star_filter == rhs.metadata_star_filter &&
           lhs.midi_keyboard_scale == rhs.midi_keyboard_scale &&
           lhs.midi_keyboard_key == rhs.midi_keyboard_key &&
           lhs.midi_keyboard_typing_octave == rhs.midi_keyboard_typing_octave &&
           lhs.midi_keyboard_layout == rhs.midi_keyboard_layout &&
           lhs.custom_typing_layout_keys == rhs.custom_typing_layout_keys &&
           lhs.custom_typing_octave_down_key ==
               rhs.custom_typing_octave_down_key &&
           lhs.custom_typing_octave_up_key == rhs.custom_typing_octave_up_key;
  }

  friend bool operator!=(const UIPreferences &lhs, const UIPreferences &rhs) {
    return !(lhs == rhs);
  }
};

struct PreferenceData {
  /// Patch folders the user has added, in browser order.
  std::vector<std::filesystem::path> workspace_folders;
  /// Where the last save/export dialog was pointed, so the next one starts
  /// there rather than at a fixed location.
  std::filesystem::path last_save_directory;
  bool show_builtin_presets = true;

  // Runtime-only flag set while adopting a pre-workspace patch folder.
  bool migrated_legacy_workspace = false;
  // Persisted schema marker. Older and temporarily incompatible releases did
  // not write it, even if they had already written workspace_folders: [].
  bool legacy_workspace_migration_complete = false;

  // The SQLite-to-sidecar migration has its own marker because workspace
  // releases existed that discarded data_directory without importing stars.
  bool legacy_metadata_migration_complete = false;
  // Runtime-only exact destination recovered from an old data_directory.
  std::filesystem::path legacy_metadata_workspace;

  ui::styles::ThemeId theme = ui::styles::ThemeId::MegatoyDark;
  UIPreferences ui_preferences;
};
