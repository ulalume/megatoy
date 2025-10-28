#pragma once

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
  bool initialized = false;

  std::map<ImGuiKey, ym2612::Note> key_mappings;
  std::map<ym2612::Note, ImGuiKey> reverse_mappings;
  std::vector<Key> scale_keys;
  std::vector<ym2612::Note> display_notes;
  std::string typing_range_label;
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
