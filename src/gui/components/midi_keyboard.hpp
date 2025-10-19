#pragma once

#include "app_state.hpp"
#include <imgui.h>

namespace ui {
// Function to render the instrument settings panel
void render_midi_keyboard(const char *title, AppState &app_state);

} // namespace ui
