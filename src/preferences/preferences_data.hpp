#pragma once

#include "audio/load_meter.hpp"
#include "core/types.hpp"
#include "preferences/theme.hpp"
#include "preferences/typing_keyboard_layout.hpp"
#include "ym2612/chip.hpp"
#include <filesystem>
#include <string>
#include <vector>

struct UIPreferences {
  bool show_patch_editor = true;
  bool show_midi_keyboard = true;
  bool show_patch_selector = true;
  bool show_mml_console = true;
  bool show_preferences = true;
  bool show_waveform = true;
  bool show_patch_lab = true;
  int ym2612_chip_type = 0;
  /// 0 = Nuked-OPN2, 1 = ymfm, matching ym2612::CoreType.
  int ym2612_core = static_cast<int>(ym2612::CoreType::Ymfm);
  /**
   * Frames per audio callback; 0 keeps the platform's default. Smaller means
   * less latency and less room to absorb a stall. Changing it reopens the
   * audio device.
   */
  int audio_buffer_frames = 0;
  /// 0 = peak, 1 = peak and average, matching audio::LoadReading.
  int audio_load_reading = static_cast<int>(audio::LoadReading::Peak);
  /**
   * Factor the interface is drawn at; 0 follows the display. Values outside
   * the supported range are clamped when the style is built.
   */
  float ui_scale = 0.0f;
  bool use_velocity = true;
  int velocity_sensitivity_depth = 100;
  bool use_pitch_bend = true;
  bool use_mod_wheel = false;
  bool steal_oldest_note_when_full = true;
  /**
   * How an edit spreads across selected operators. Relative keeps the
   * distance between them; absolute lands them on the same value. Booleans
   * and the SSG envelope type are always absolute.
   */
  bool multi_operator_edit_absolute = false;
  /**
   * The MIDI note every envelope graph is drawn at. The graph shows a shape
   * rather than a performance, so the note is a setting: 60 is middle C, and
   * ui::envelope clamps it to C0..B7.
   */
  int envelope_reference_midi_note = 60;

  // Patch selector view settings
  std::string metadata_search_query;
  int metadata_star_filter = 0;

  int midi_keyboard_scale = static_cast<int>(Scale::CHROMATIC);
  int midi_keyboard_key = static_cast<int>(Key::C);
  int midi_keyboard_typing_octave = 4;
  int midi_keyboard_layout = static_cast<int>(ui::TypingKeyboardLayout::Qwerty);
  ui::TypingLayoutPreference custom_typing_layout_keys =
      ui::TypingLayoutPreference(ui::default_typing_layout_keys.begin(),
                                 ui::default_typing_layout_keys.end());
  int custom_typing_octave_down_key = ui::default_typing_octave_down_key;
  int custom_typing_octave_up_key = ui::default_typing_octave_up_key;

  friend bool operator==(const UIPreferences &lhs, const UIPreferences &rhs) {
    // Search and star filters are session-scoped. Excluding them prevents
    // typing in the patch browser from dirtying the persistent store.
    return lhs.show_patch_editor == rhs.show_patch_editor &&
           lhs.show_midi_keyboard == rhs.show_midi_keyboard &&
           lhs.show_patch_selector == rhs.show_patch_selector &&
           lhs.show_mml_console == rhs.show_mml_console &&
           lhs.show_preferences == rhs.show_preferences &&
           lhs.show_waveform == rhs.show_waveform &&
           lhs.show_patch_lab == rhs.show_patch_lab &&
           lhs.ym2612_chip_type == rhs.ym2612_chip_type &&
           lhs.ym2612_core == rhs.ym2612_core &&
           lhs.audio_buffer_frames == rhs.audio_buffer_frames &&
           lhs.audio_load_reading == rhs.audio_load_reading &&
           lhs.ui_scale == rhs.ui_scale &&
           lhs.use_velocity == rhs.use_velocity &&
           lhs.velocity_sensitivity_depth == rhs.velocity_sensitivity_depth &&
           lhs.steal_oldest_note_when_full == rhs.steal_oldest_note_when_full &&
           lhs.multi_operator_edit_absolute ==
               rhs.multi_operator_edit_absolute &&
           lhs.envelope_reference_midi_note ==
               rhs.envelope_reference_midi_note &&
           lhs.use_pitch_bend == rhs.use_pitch_bend &&
           lhs.use_mod_wheel == rhs.use_mod_wheel &&
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

/// One patch left open in the editor: its browser entry, and whether it was
/// kept open or only previewed.
struct OpenPatchRecord {
  std::string path;
  bool pinned = true;
  bool operator==(const OpenPatchRecord &) const = default;
};

struct PreferenceData {
  /// Patch folders the user has added, in browser order.
  std::vector<std::filesystem::path> workspace_folders;
  /// Where the last save/export dialog was pointed, so the next one starts
  /// there rather than at a fixed location.
  std::filesystem::path last_save_directory;
  /// Repository-relative source path of the patch selected most recently.
  std::string last_patch_path;
  /// The patches open in the editor, in tab order, and which one was active
  /// (-1 for none).
  std::vector<OpenPatchRecord> open_patches;
  int active_open_patch = -1;
  /**
   * The version whose change log the user has already been offered. Empty on
   * a fresh install and on builds that predate the change log. Written as
   * soon as the decision is made, so ignoring the notice still counts.
   */
  std::string last_seen_version;
  bool show_builtin_presets = true;

  ui::styles::ThemeId theme = ui::styles::ThemeId::MegatoyDark;
  UIPreferences ui_preferences;
};
