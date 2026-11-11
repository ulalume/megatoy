#pragma once

#include <imgui.h>

namespace ui {

/**
 * The shape of SSG-EG envelope `envelope_type` (0-7) over four of its
 * cycles, its outline in the envelope graph's colour over a fill of it, in a
 * frame. "OFF" while `enabled` is false. Takes `size` as an item.
 */
void ssg_diagram(bool enabled, int envelope_type, ImVec2 size);

} // namespace ui
