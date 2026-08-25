#pragma once

#include "app_state.hpp"
#include "gui/ui_scale.hpp"
#include "ym2612/patch.hpp"
#include "ym2612/types.hpp"
#include <imgui.h>

namespace ui {

inline float vslider_width() { return ui::scale::px(20.0f); }
inline float vslider_height() { return ui::scale::px(102.0f); }
inline ImVec2 vslider_size() {
  return ImVec2(vslider_width(), vslider_height());
}
/// Six vertical sliders and the gaps between them.
inline float hslider_width() {
  return vslider_width() * 6 + ImGui::GetStyle().ItemSpacing.x * 5;
}
inline ImVec2 image_size() { return ImVec2(hslider_width(), vslider_height()); }

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
