#pragma once

#include "preferences/preference_manager.hpp"
#include "ym2612/note.hpp"
#include <functional>
#include <imgui.h>
#include <vector>

struct InputState;

namespace ui {

struct MidiKeyboardContext {
  PreferenceManager::UIPreferences &ui_prefs;
  InputState &input_state;
  std::function<bool(ym2612::Note, uint8_t)> key_on;
  std::function<bool(ym2612::Note)> key_off;
  std::function<bool(const ym2612::Note &)> key_is_pressed;
  std::function<std::vector<ym2612::Note>()> active_notes;
};

void render_midi_keyboard(const char *title, MidiKeyboardContext &context);

} // namespace ui
