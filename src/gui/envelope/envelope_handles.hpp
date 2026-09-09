#pragma once

/**
 * The envelope graph's drag handles, from ym2612_eg. Only two things it
 * cannot know are here: which of megatoy's register fields a handle write
 * names, and the pixel type the graph is drawn in.
 */

#include "gui/envelope/envelope_curve.hpp"
#include "ym2612/operator_edit.hpp"

#include <ym2612_eg/handles.hpp>

#include <imgui.h>

namespace ui::envelope {

using ym2612_eg::graph::EnvelopeHandle;
using ym2612_eg::graph::EnvelopeHandles;
using ym2612_eg::graph::HandleEdit;
using ym2612_eg::graph::HandleField;
using ym2612_eg::graph::HandleGrab;
using ym2612_eg::graph::HandleIndex;
using ym2612_eg::graph::HandleMetrics;
using ym2612_eg::graph::PlotArea;
using ym2612_eg::graph::Point;

using ym2612_eg::graph::kAttackHandle;
using ym2612_eg::graph::kDecayHandle;
using ym2612_eg::graph::kFullScale;
using ym2612_eg::graph::kReleaseHandle;
using ym2612_eg::graph::kSustainHandle;

using ym2612_eg::graph::drag_handle;
using ym2612_eg::graph::grab_handle;
using ym2612_eg::graph::handle_layout;
using ym2612_eg::graph::nearest_handle;

inline ImVec2 to_imvec(const Point &point) { return ImVec2(point.x, point.y); }
inline Point to_point(const ImVec2 &pos) { return Point{pos.x, pos.y}; }

/// The register a handle write asks for.
inline ym2612::OperatorField field_of(HandleField field) {
  switch (field) {
  case HandleField::AttackRate:
    return ym2612::OperatorField::AttackRate;
  case HandleField::DecayRate:
    return ym2612::OperatorField::DecayRate;
  case HandleField::SustainLevel:
    return ym2612::OperatorField::SustainLevel;
  case HandleField::SustainRate:
    return ym2612::OperatorField::SustainRate;
  case HandleField::ReleaseRate:
    return ym2612::OperatorField::ReleaseRate;
  case HandleField::TotalLevel:
    break;
  }
  return ym2612::OperatorField::TotalLevel;
}

} // namespace ui::envelope
