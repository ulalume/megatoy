#pragma once

/**
 * One operator's registers turned into two drawable, millisecond-accurate
 * traces.
 *
 * The shapes come from ym2612_eg, a sample-accurate simulator of the chip's
 * envelope generator; everything here is the policy around it -- which note to
 * draw at, how much of the held envelope is worth showing, how wide the time
 * axis should be, and which single warning is worth a line of text.
 *
 * The graph draws two *independent* traces on one elapsed-time axis, because
 * chaining them would mean inventing a key-off instant, and a made-up key-off
 * makes both halves wrong at once: it cuts the sustain short at an arbitrary
 * time and then starts the release from whatever level that fiction produced.
 * So:
 *
 *   - the held envelope is simulated with the key never released, which is the
 *     only way SR reads truthfully (SR = 0 holds flat, SR > 0 crawls);
 *   - the release is simulated on its own, from full volume, and answers "if
 *     the note were let go now, how fast does it fall?".
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

/**
 * How much of the held envelope is worth drawing, in ms, decided from a
 * held-forever probe run. See envelope_curve.cpp for the policy; in short: a
 * sustain segment is always visible, a 27-second sustain decay is cut short
 * rather than allowed to swallow the graph, and an SSG loop gets about three
 * and a half periods.
 *
 * Nothing here is a key-off: the trace simply stops being drawn. Whatever the
 * envelope was doing at that instant carries on to the right edge.
 */
double choose_held_ms(const ym2612_eg::CurveResult &probe,
                      const ym2612_eg::OperatorParams &op);

/// How far ahead the held-forever probe looks for this patch.
double probe_max_ms(const ym2612_eg::OperatorParams &op);

/**
 * How long the release is simulated for. Bounded by what the axis could show
 * it at anyway (see the span budget in envelope_curve.cpp), with a floor so
 * the ordinary release rates still measure their true length and a ceiling
 * because RR = 0 never reaches silence at all.
 */
double release_max_ms(double held_ms);

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
  /// Attack, decay and sustain with the key never released: drawn as a line.
  ym2612_eg::CurveResult held;
  /// A release from full volume, starting at t = 0: drawn as a filled area.
  ym2612_eg::CurveResult release;

  double span_ms = 0.0; ///< quantised width of the time axis
  double held_ms = 0.0; ///< how much of the held envelope the policy asked for
  double held_content_ms = 0.0;    ///< where the held polyline actually ends
  double release_content_ms = 0.0; ///< where the release polyline actually ends

  /// The held envelope came to rest -- an SR = 0 hold, a frozen attack, a
  /// sustain that reached silence -- so the trace is continued to the right
  /// edge flat. Otherwise it ran out of budget while still moving and is
  /// continued along its final slope, which is exact: every post-attack
  /// segment is linear in attenuation.
  bool held_parked = false;
  /// The release was still falling when its budget ran out.
  bool release_truncated = false;

  // Segment boundaries on the held trace, ms; negative when the segment never
  // happened.
  double attack_end_ms = -1.0;
  double decay_end_ms = -1.0;

  uint16_t peak_out = 0;    ///< output attenuation at full volume (TL * 8)
  uint16_t sustain_out = 0; ///< output attenuation the decay aims at (SL + TL)

  const char *warning = nullptr;
};

/**
 * Three passes over the simulator: a held-forever probe to discover park time,
 * loop frequency and warnings; the held trace itself, cut to the width the
 * probe justified; and a release from full volume, which shares only the time
 * axis with the other two.
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
