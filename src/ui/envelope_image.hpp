#pragma once

#include "../app_state.hpp"
#include "../ym2612/types.hpp"
#include <imgui.h>

namespace ui {
void render_envelope_image(const ym2612::OperatorSettings &op,
                           const UIState::EnvelopeState &state, ImVec2 size);
} // namespace ui
