#pragma once

/**
 * The envelope graph's drag handles: where each one sits, which of them can be
 * dragged honestly, and what a drag off one is aiming at. Milliseconds,
 * attenuation and pixels only -- envelope_image draws them, operator_editor
 * puts the buttons on them and writes the registers, and neither decision is
 * made here.
 */

#include "gui/envelope/envelope_curve.hpp"
#include "ym2612/operator_edit.hpp"

#include <algorithm>
#include <imgui.h>

namespace ui::envelope {

using ym2612_eg::graph::curve_out_at_ms;

/// Attenuation at the bottom of the graph; 0 (full volume) is at the top.
inline constexpr double kFullScale =
    static_cast<double>(ym2612_eg::kMaxAttenuation);

/// A handle is named for the part of the envelope it is grabbed by, and
/// stands for the one or two parameters that part is made of.
enum HandleIndex {
  kAttackHandle = 0, ///< AttackRate across, TotalLevel down
  kDecayHandle,      ///< DecayRate across, SustainLevel down
  kSustainHandle,    ///< SustainRate down
  kReleaseHandle,    ///< ReleaseRate across
  kHandleCount,
};

/// The plot in pixels against the units the curve is drawn in: milliseconds
/// across, attenuation down.
struct PlotArea {
  ImVec2 min;
  ImVec2 max;
  /// The width being drawn, which stands still while a handle is held.
  double span_ms = 1.0;

  float width() const { return std::max(max.x - min.x, 1.0f); }
  float height() const { return std::max(max.y - min.y, 1.0f); }

  float x_of(double ms) const {
    const double t = std::clamp(ms / span_ms, 0.0, 1.0);
    return min.x + static_cast<float>(t) * width();
  }
  float y_of(double out) const {
    const double t = std::clamp(out / kFullScale, 0.0, 1.0);
    return min.y + static_cast<float>(t) * height();
  }
  ImVec2 at(double ms, double out) const { return ImVec2(x_of(ms), y_of(out)); }

  /// What one pixel of pointer movement is worth.
  double ms_per_px() const { return span_ms / static_cast<double>(width()); }
  double out_per_px() const { return kFullScale / static_cast<double>(height()); }
};

/// The sizes a handle is drawn and grabbed at, in the pixels the graph is
/// drawn in, and the least room a sustain has to have to carry one.
struct HandleMetrics {
  float radius = 3.0f;
  float grab = 6.0f;
  float min_sustain_height = 6.0f;
  float min_sustain_width = 8.0f;
};

struct EnvelopeHandle {
  /// False for a handle that cannot be dragged honestly: it is neither drawn
  /// nor hit-tested.
  bool shown = false;
  /// Where it is drawn, clamped so the whole dot stays inside the plot.
  ImVec2 pos;
  /// What the across parameter's solver is asked about: the length of that
  /// phase alone, or -- for the sustain, which has no end to grab -- how far
  /// into the sustain `pos` reads the line.
  double ms = 0.0;
  /// What the down parameter's solver is asked about.
  double out = 0.0;
};

struct EnvelopeHandles {
  PlotArea plot;
  HandleMetrics metrics;
  EnvelopeHandle items[kHandleCount];
};

/// Where the sustain handle reads the line: half way between the end of the
/// decay and the right-hand edge.
double sustain_probe_ms(const EnvelopeCurve &curve, double span_ms);

/// The handle nearest `pos` and within its grab box, or `kHandleCount` for
/// none. Two handles can stand close enough to share a grab box -- a shallow
/// sustain level puts the knee just under the peak -- so which one answers is
/// decided by distance rather than by whichever is offered first.
HandleIndex nearest_handle(const EnvelopeHandles &handles, ImVec2 pos);

EnvelopeHandles handle_layout(const EnvelopeCurve &curve, const PlotArea &plot,
                              bool ssg_enabled, const HandleMetrics &metrics);

/**
 * A drag is measured from where it was grabbed rather than from where the
 * pointer is: `moved_px` is the distance travelled since, and the value it
 * arrives at is the one the handle was grabbed at plus that much. A pointer
 * that has not moved therefore asks for the value the handle already holds.
 */
double dragged_ms(const PlotArea &plot, double grabbed_ms, float moved_px);
double dragged_out(const PlotArea &plot, double grabbed_out, float moved_px);

/**
 * The register `field` takes to land nearest `target` -- milliseconds for a
 * rate dragged along the axis, attenuation for a level dragged up and down.
 * `elapsed_ms` is how far into the sustain the level was read and means
 * nothing to the other fields; a field with no handle keeps its value.
 */
int solve_operator_field(const ym2612::OperatorSettings &op,
                         ym2612::OperatorField field, double target,
                         double elapsed_ms);

} // namespace ui::envelope
