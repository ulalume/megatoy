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

struct PatchEditorContext;

/**
 * Draw one operator, addressed by display slot: 0..3 is OP1..OP4 as the user
 * reads them, not the order the chip stores them in.
 *
 * The operator draws itself inside a border that doubles as its selection
 * state, and every widget in it feeds the selection: see
 * note_operator_edit() for what an edit does to the group.
 */
void render_operator_editor(PatchEditorContext &context, ym2612::Patch &patch,
                            int slot, UIState::EnvelopeState &envelope_state,
                            bool space_for_feedback);
} // namespace ui
