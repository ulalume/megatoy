#pragma once

#include "app_state.hpp"
#include <imgui.h>

namespace ui {
const std::map<ImGuiKey, ym2612::Note>
create_key_mappings(Scale scale, Key key, uint8_t selected_octave);

void check_keyboard_typing(AppState &app_state,
                           const std::map<ImGuiKey, ym2612::Note> key_mappings);

// Function to render the main audio control panel
void render_keyboard_typing(const char *title, AppState &app_state);
} // namespace ui
