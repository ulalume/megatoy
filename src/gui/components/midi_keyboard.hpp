#pragma once

#include "gui/components/bar_centre_fit.hpp"
#include "gui/input/typing_keyboard_layout.hpp"
#include "input_state.hpp"
#include "preferences/preference_manager.hpp"
#include "ym2612/note.hpp"
#include <cstdint>
#include <functional>
#include <imgui.h>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ui {

struct MidiKeyboardState {
  Scale cached_scale = Scale::CHROMATIC;
  Key cached_key = Key::C;
  uint8_t cached_octave = 0;
  TypingKeyboardLayout cached_layout = TypingKeyboardLayout::Auto;
  TypingLayout cached_custom_layout;
  struct PlaybackState {
    bool playing = false;
    float timer = 0.0f;
    std::size_t next_index = 0;
    bool note_active = false;
    ym2612::Note current_note{};
    std::vector<ym2612::Note> sequence_snapshot;
  } playback;
  bool initialized = false;
  /// The keys this keyboard turned on, under the mouse and the fingers.
  std::vector<ym2612::Note> held_notes;
  /// Each finger on the screen: whether it came down on the keyboard, which
  /// is what lets it play.
  std::map<std::uint64_t, bool> fingers_on_keyboard;

  std::map<ImGuiKey, ym2612::Note> key_mappings;
  std::map<ym2612::Note, ImGuiKey> reverse_mappings;
  std::vector<Key> scale_keys;
  std::vector<ym2612::Note> display_notes;
  std::string typing_range_label;
  /// Set when the keys typing plays change, so the keyboard scrolls to show
  /// them.
  bool typing_range_moved = false;
  std::vector<ym2612::Note> playback_sequence;
};

struct MidiKeyboardContext {
  PreferenceManager::UIPreferences &ui_prefs;
  InputState &input_state;
  MidiKeyboardState &state;
  std::function<bool(ym2612::Note, uint8_t)> key_on;
  std::function<bool(ym2612::Note)> key_off;
  std::function<bool(const ym2612::Note &)> key_is_pressed;
  std::function<std::vector<ym2612::Note>()> active_notes;
};

void render_midi_keyboard(const char *title, MidiKeyboardContext &context);

/// The size the scale's Play switch takes in the menu bar at `fit`; the
/// switch is what is centred, the note follows it.
BarCentreSize scale_play_size(const MidiKeyboardContext &context,
                              BarCentreFit fit);

/**
 * The switch that plays the keyboard's scale, drawn like the menu bar's menus
 * on their row at `row_y` and centred between `left` and `right`, with the
 * scale's name beside it at the full fit and its icon alone at the least. It
 * keeps one width and place whether it reads Play or Stop.
 */
void render_scale_play(MidiKeyboardContext &context, BarCentreFit fit,
                       float left, float right, float row_y);

} // namespace ui
