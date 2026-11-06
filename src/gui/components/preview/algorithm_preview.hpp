#pragma once

#include <imgui.h>

namespace ui {

/**
 * The connections of `algorithm` (0-7) as megamml draws them: OP1 to OP4 as
 * numbered boxes, modulators feeding the operators they modulate and the
 * carriers, filled in the carrier colour, out to the right. Takes `size`
 * as an item.
 */
void algorithm_diagram(int algorithm, ImVec2 size);

} // namespace ui
