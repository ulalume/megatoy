#pragma once

#include "gui/styles/theme.hpp"

#include <filesystem>
#include <string>

struct UIPreferences {
  bool show_patch_editor = true;
  bool show_audio_controls = true;
  bool show_midi_keyboard = true;
  bool show_patch_selector = true;
  bool show_mml_console = false;
  bool show_preferences = false;
  bool show_waveform = true;
  bool use_velocity = true;
  bool steal_oldest_note_when_full = true;
  std::string patch_search_query;

  // Patch selector view settings
  int patch_view_mode = 0; // 0=Tree, 1=Search, 2=Table
  int patch_sort_column =
      0; // 0=Name, 1=Category, 2=StarRating, 3=Format, 4=Path
  int patch_sort_order = 0; // 0=Ascending, 1=Descending
  std::string metadata_search_query;
  int metadata_star_filter = 0;

  friend bool operator==(const UIPreferences &lhs, const UIPreferences &rhs) {
    return lhs.show_patch_editor == rhs.show_patch_editor &&
           lhs.show_audio_controls == rhs.show_audio_controls &&
           lhs.show_midi_keyboard == rhs.show_midi_keyboard &&
           lhs.show_patch_selector == rhs.show_patch_selector &&
           lhs.show_mml_console == rhs.show_mml_console &&
           lhs.show_preferences == rhs.show_preferences &&
           lhs.show_waveform == rhs.show_waveform &&
           lhs.use_velocity == rhs.use_velocity &&
           lhs.steal_oldest_note_when_full == rhs.steal_oldest_note_when_full &&
           lhs.patch_search_query == rhs.patch_search_query &&
           lhs.patch_view_mode == rhs.patch_view_mode &&
           lhs.patch_sort_column == rhs.patch_sort_column &&
           lhs.patch_sort_order == rhs.patch_sort_order &&
           lhs.metadata_search_query == rhs.metadata_search_query &&
           lhs.metadata_star_filter == rhs.metadata_star_filter;
  }

  friend bool operator!=(const UIPreferences &lhs, const UIPreferences &rhs) {
    return !(lhs == rhs);
  }
};

struct PreferenceData {
  std::filesystem::path data_directory;
  ui::styles::ThemeId theme = ui::styles::ThemeId::MegatoyDark;
  UIPreferences ui_preferences;
};
