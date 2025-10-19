#pragma once

#include "app_state.hpp"
#include <imgui.h>

namespace ui {

// Function to render the main audio control panel
void render_keyboard_typing(const char *title, AppState &app_state);
} // namespace ui
