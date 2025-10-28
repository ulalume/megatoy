#pragma once

#include "core/types.hpp"
#include "ym2612/note.hpp"
#include <functional>
#include <imgui.h>
#include <map>

struct InputState;

namespace ui {
const std::map<ImGuiKey, ym2612::Note>
create_key_mappings(Scale scale, Key key, uint8_t selected_octave);

struct KeyboardTypingContext {
  InputState &input_state;
  std::function<void(ym2612::Note, uint8_t)> key_on;
  std::function<void(ym2612::Note)> key_off;
};

void check_keyboard_typing(KeyboardTypingContext &context,
                           const std::map<ImGuiKey, ym2612::Note> key_mappings);

} // namespace ui
