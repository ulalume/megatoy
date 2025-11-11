#pragma once

#include "app_state.hpp"
#include "ym2612/patch.hpp"
#include "ym2612/types.hpp"
#include <imgui.h>

namespace ui {

const float vslider_width = 20;
const float vslider_height = 102;
const ImVec2 vslider_size(vslider_width, vslider_height);
const float hslider_width = vslider_width * 6 + 8 * 5;
const ImVec2 image_size(hslider_width, vslider_height);
// Helper function to render operator settings
struct PatchEditorContext;

bool render_operator_editor(PatchEditorContext &context, ym2612::Patch &patch,
                            ym2612::OperatorSettings &op, int op_index,
                            UIState::EnvelopeState &envelope_state,
                            bool space_for_feedback);
} // namespace ui
