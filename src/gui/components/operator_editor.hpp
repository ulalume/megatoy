#pragma once

#include "app_state.hpp"
#include "gui/components/envelope_image.hpp"
#include "gui/ui_scale.hpp"
#include "ym2612/patch.hpp"
#include "ym2612/types.hpp"
#include <imgui.h>

namespace ui {

/// A vertical slider is narrower than a horizontal one is tall, and they
/// stand as far apart as horizontal ones do.
inline float vslider_width() { return ImGui::GetFontSize() + scale::px(8.0f); }
inline float vslider_gap() { return ImGui::GetStyle().ItemSpacing.y; }
/// As tall as the column beside the sliders: five rows of controls, the
/// spacing between them and one extra break.
inline float vslider_height() {
  return (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y) * 5.0f;
}
/// Room for a handle dragged through attenuation.
inline float envelope_graph_height() { return ui::scale::px(128.0f); }
inline ImVec2 vslider_size() {
  return ImVec2(vslider_width(), vslider_height());
}
/// Six vertical sliders and the gaps between them.
inline float hslider_width() { return vslider_width() * 6 + vslider_gap() * 5; }

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
                            const EnvelopeVoices &voices);

/// Give each selected operator what the others in the selection light, as
/// UIState::EnvelopeState::echoed. Called before the operators are drawn.
void echo_selection_highlights(PatchEditorContext &context);
} // namespace ui
