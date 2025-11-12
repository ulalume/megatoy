#pragma once

#include "gui/input/typing_keyboard_layout.hpp"
#include "input_state.hpp"
#include "preferences/preference_manager.hpp"
#include "ym2612/note.hpp"
#include <functional>
#include <imgui.h>
#include <map>
#include <string>
#include <vector>

namespace ui {

struct MidiKeyboardState {
  Scale cached_scale = Scale::CHROMATIC;
  Key cached_key = Key::C;
  uint8_t cached_octave = 0;
  TypingKeyboardLayout cached_layout = TypingKeyboardLayout::Qwerty;
  TypingLayout cached_custom_layout =
      builtin_typing_layout(TypingKeyboardLayout::Qwerty);
  struct PlaybackState {
    bool playing = false;
    float timer = 0.0f;
    std::size_t next_index = 0;
    bool note_active = false;
    ym2612::Note current_note{};
    std::vector<ym2612::Note> sequence_snapshot;
  } playback;
  bool initialized = false;

  std::map<ImGuiKey, ym2612::Note> key_mappings;
  std::map<ym2612::Note, ImGuiKey> reverse_mappings;
  std::vector<Key> scale_keys;
  std::vector<ym2612::Note> display_notes;
  std::string typing_range_label;
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

} // namespace ui
