#pragma once

#include "core/types.hpp"
#include "input_state.hpp"
#include "typing_keyboard_layout.hpp"
#include "ym2612/note.hpp"
#include <functional>
#include <imgui.h>
#include <map>

namespace ui {
const std::map<ImGuiKey, ym2612::Note>
create_key_mappings(Scale scale, Key key, uint8_t selected_octave,
                    TypingKeyboardLayout layout);

struct KeyboardTypingContext {
  InputState &input_state;
  std::function<void(ym2612::Note, uint8_t)> key_on;
  std::function<void(ym2612::Note)> key_off;
};

void check_keyboard_typing(KeyboardTypingContext &context,
                           const std::map<ImGuiKey, ym2612::Note> key_mappings);

} // namespace ui
