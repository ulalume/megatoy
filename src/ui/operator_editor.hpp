#pragma once

#include "../app_state.hpp"
#include "../ym2612/types.hpp"
#include <imgui.h>

namespace ui {
// Helper function to render operator settings
void render_operator_editor(AppState &app_state, ym2612::OperatorSettings &op,
                            int op_num);
} // namespace ui
