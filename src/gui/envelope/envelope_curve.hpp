#pragma once

/**
 * One operator's registers turned into a drawable, millisecond-accurate
 * envelope.
 *
 * The shape itself comes from ym2612_eg, a sample-accurate simulator of the
 * chip's envelope generator; everything here is the policy around it -- which
 * note to draw at, how long to hold the key, how wide the time axis should be,
 * and which single warning is worth a line of text.
 *
 * Deliberately free of ImGui so the policy can be unit-tested on its own
 * (tests/gui/envelope_curve_test.cpp). The drawing lives in
 * gui/components/envelope_image.cpp.
 */

#include "ym2612/types.hpp"

#include <ym2612_eg/ym2612_eg.hpp>

#include <cstdint>

namespace ui::envelope {

/**
 * The single reference note every envelope graph is drawn at.
 *
 * The graph shows a shape, not a performance: with KS = 0 -- what most patches
 * use -- the curve barely depends on pitch, and a graph that followed the
 * keyboard would rescale itself under the user's hands while they edit.
 *
 * This is the ONLY place the note is decided. Wiring it to a preference (or to
 * the last played note) later means changing reference_pitch(), nothing else.
 */
inline constexpr int kReferenceMidiNote = 60; // middle C
ym2612_eg::NotePitch reference_pitch();

/**
 * megatoy stores the SSG-EG enable bit and the 3-bit shape separately; the
 * chip register (and ym2612_eg) wants one nibble:
 * bit3 enable, bit2 attack, bit1 alternate, bit0 hold.
 */
uint8_t packed_ssg(const ym2612::OperatorSettings &op);

ym2612_eg::OperatorParams
to_operator_params(const ym2612::OperatorSettings &op);

bool same_envelope(const ym2612_eg::OperatorParams &lhs,
                   const ym2612_eg::OperatorParams &rhs);

/// How long the key is held, and how long to simulate in total.
struct Gate {
  double gate_ms = 0.0;
  double max_ms = 0.0;
};

/**
 * Pick the gate from a held-forever probe run. See envelope_curve.cpp for the
 * policy; in short: a sustain segment is always visible, a 27-second sustain
 * decay is cut short rather than allowed to swallow the graph, and an SSG
 * loop gets about three and a half periods before the key comes up.
 */
Gate choose_gate(const ym2612_eg::CurveResult &probe,
                 const ym2612_eg::OperatorParams &op);

/// How far ahead the held-forever probe looks for this patch.
double probe_max_ms(const ym2612_eg::OperatorParams &op);

/**
 * The drawn width of the time axis, in ms, quantised to a ladder of round
 * values with hysteresis so that dragging a slider cannot make the axis
 * breathe. `current_span_ms` is the span drawn last frame; 0 asks for a fresh
 * fit.
 */
double quantize_span_ms(double content_ms, double current_span_ms);

/// The grid/label interval for a given span: a round number, 3-6 divisions.
double grid_step_ms(double span_ms);

/**
 * The one warning worth showing, or nullptr. Ordered by how badly the patch is
 * misbehaving, because only one line is ever drawn.
 */
const char *warning_line(const ym2612_eg::CurveResult &curve,
                         const ym2612_eg::OperatorParams &op);

/// Everything the graph needs for one operator.
struct EnvelopeCurve {
  ym2612_eg::CurveResult curve; ///< gated: attack .. release, one polyline
  double gate_ms = 0.0;
  double span_ms = 0.0;   ///< quantised width of the time axis
  double content_ms = 0.0; ///< where the polyline actually ends
  /// The envelope was still moving when the simulation ran out of budget, so
  /// the last segment should be extended to the right edge rather than left
  /// hanging in mid-air. Post-attack segments are linear in attenuation, so
  /// the extension is as accurate as the rest of the curve.
  bool truncated = false;

  // Segment boundaries, ms; negative when the segment never happened.
  double attack_end_ms = -1.0;
  double decay_end_ms = -1.0;
  double key_off_ms = -1.0;

  uint16_t peak_out = 0;    ///< output attenuation at full volume (TL * 8)
  uint16_t sustain_out = 0; ///< output attenuation the decay aims at (SL + TL)

  const char *warning = nullptr;
};

/**
 * Two passes over the simulator: one held forever to discover park time, loop
 * frequency and warnings, then one with the chosen gate so the release is
 * drawn chained onto the held part.
 */
EnvelopeCurve build_envelope_curve(const ym2612::OperatorSettings &op,
                                   double previous_span_ms);

/**
 * Remembers one operator's curve and rebuilds it only when the registers that
 * shape it (or the reference note) actually change -- the same
 * compare-then-recompute pattern PatchSession uses for audio settings.
 */
class EnvelopeCurveCache {
public:
  const EnvelopeCurve &get(const ym2612::OperatorSettings &op);

  /// Rebuild count, for tests.
  int rebuild_count() const { return rebuilds_; }

private:
  ym2612_eg::OperatorParams params_{};
  ym2612_eg::NotePitch pitch_{};
  EnvelopeCurve curve_;
  bool valid_ = false;
  int rebuilds_ = 0;
};

} // namespace ui::envelope
