#pragma once

#include "../ym2612/types.hpp"
#include <imgui.h>

namespace ui {
// Helper function to render operator settings
void render_envelope_image(const ym2612::OperatorSettings &op, ImVec2 size);
} // namespace ui
