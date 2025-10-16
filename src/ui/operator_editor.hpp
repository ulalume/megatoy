#pragma once

#include "../app_state.hpp"
#include "../ym2612/types.hpp"
#include <imgui.h>

namespace ui {

const float vslider_width = 20;
const float vslider_height = 102;
const ImVec2 vslider_size(vslider_width, vslider_height);
const float hslider_width = vslider_width * 6 + 8 * 5;
const ImVec2 image_size(hslider_width, vslider_height);
// Helper function to render operator settings
bool render_operator_editor(AppState &app_state, ym2612::OperatorSettings &op,
                            int op_index);
} // namespace ui
