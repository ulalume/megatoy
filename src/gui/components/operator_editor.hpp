#pragma once

#include "app_state.hpp"
#include "gui/components/envelope_image.hpp"
#include "gui/ui_scale.hpp"
#include "ym2612/patch.hpp"
#include "ym2612/types.hpp"
#include <cmath>
#include <imgui.h>

namespace ui {

/// The vertical sliders stand a pixel apart, and share out among them the
/// room six of them a text height and 8 px wide, at the spacing between
/// items, would take, on whole pixels.
inline float vslider_gap() { return scale::px(1.0f); }
inline float vslider_width() {
  const float room = (ImGui::GetFontSize() + scale::px(8.0f)) * 6.0f +
                     ImGui::GetStyle().ItemSpacing.y * 5.0f;
  return std::floor((room - vslider_gap() * 5.0f) / 6.0f);
}
/// The room between the rows of the column beside the sliders.
inline float side_row_gap() { return scale::px(1.0f); }
/// As tall as the column beside the sliders: five rows of controls, three of
/// them side_row_gap() under the one above, and the SSG-EG ones and the rest
/// the spacing between items apart.
inline float vslider_height() {
  return ImGui::GetFrameHeight() * 5.0f + side_row_gap() * 3.0f +
         ImGui::GetStyle().ItemSpacing.y;
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
