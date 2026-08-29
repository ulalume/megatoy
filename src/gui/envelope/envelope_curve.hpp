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
 * keyboard would rescale itself under the user's hands while they edit. So it
 * is a setting the user changes deliberately, not something the playing moves.
 *
 * This is still the ONLY place the note is decided: everything downstream asks
 * reference_pitch(). The app pushes the preference in through
 * set_reference_midi_note(), which is what keeps ImGui and the preference
 * headers out of this file -- the policy is unit-tested without either.
 */
inline constexpr int kDefaultReferenceMidiNote = 60; // middle C
/// C0 to B7: megatoy's own note range, one F-num block each. Above B7 the
/// chip has no block left and every note folds onto the same pitch.
inline constexpr int kMinReferenceMidiNote = 12;  // C0
inline constexpr int kMaxReferenceMidiNote = 107; // B7

/// However slow an SSG loop is, this much of one period stays on the axis --
/// up to kLoopHardMaxMs, past which the loop is a slow gesture rather than an
/// envelope and the axis stops following it.
inline constexpr double kLoopVisiblePeriods = 1.2;
inline constexpr double kLoopHardMaxMs = 20000.0;

/// Clamped into [kMinReferenceMidiNote, kMaxReferenceMidiNote].
void set_reference_midi_note(int midi_note);
int reference_midi_note();
ym2612_eg::NotePitch reference_pitch();

/**
 * megatoy stores the SSG-EG enable bit and the 3-bit shape separately; the
 * chip register (and ym2612_eg) wants one nibble:
 * bit3 enable, bit2 attack, bit1 alternate, bit0 hold.
 */
uint8_t packed_ssg(const ym2612::OperatorSettings &op);

ym2612_eg::OperatorParams
to_operator_params(const ym2612::OperatorSettings &op);

/**
 * The internal attenuation at which this operator is at its LOUDEST -- where
 * a release has to start from.
 *
 * Normally that is 0, the top of the scale. With SSG-EG enabled and the attack
 * bit set (types 4-7) the output is inverted, `(0x200 - att) & 0x3FF`, so the
 * scale runs the other way: 0 is the quietest point the ramp ever reaches and
 * 0x200 is full volume. Starting those patches at 0 releases from silence,
 * which the chip's key-off rules cut dead in a single sample.
 */
uint16_t loudest_attenuation(const ym2612_eg::OperatorParams &op);

bool same_envelope(const ym2612_eg::OperatorParams &lhs,
                   const ym2612_eg::OperatorParams &rhs);

/**
 * How long each phase of the held envelope lasts at `pitch`, in ms.
 *
 * Closed form rather than measured. The post-attack phases are linear in
 * attenuation, so their durations are one division each; the attack is
 * exponential, so its own recurrence is iterated -- a couple of hundred steps,
 * against the hundreds of thousands of samples the same stretch would cost to
 * simulate. Nothing here depends on how far a probe happened to look, which is
 * the whole point: a horizon is a place for the answer to change discontinuously.
 *
 * A phase whose effective rate is 0 lasts forever, and says so. SR = 0 really
 * does hold for ever, and reporting that honestly is what makes it the widest
 * axis rather than -- as a probe that gave up would have it -- indistinguishable
 * from SR = 31.
 */
struct HeldTimeline {
  double attack_ms = 0.0;
  double decay_ms = 0.0;
  double sustain_ms = 0.0;

  /// Where the sustain begins: the last structural feature the envelope has,
  /// and so the last instant the axis has to reach to be worth looking at.
  /// Infinite when a phase before it never ends.
  double sustain_start_ms() const { return attack_ms + decay_ms; }
  double lifetime_ms() const { return sustain_start_ms() + sustain_ms; }

  /// The same two, with each phase saturated at the longest one an axis can
  /// usefully hold. This is a drawing policy, not a fact about the envelope:
  /// "never decays" and "decays over a minute" look the same on any axis, and
  /// saturating rather than special-casing infinity keeps them next to each
  /// other instead of a cliff apart.
  double drawable_sustain_start_ms() const;
  double drawable_lifetime_ms() const;
};

HeldTimeline held_timeline(const ym2612_eg::OperatorParams &op,
                           ym2612_eg::NotePitch pitch);

/// The whole course of the held envelope, attack to silence.
double held_lifetime_ms(const ym2612_eg::OperatorParams &op,
                        ym2612_eg::NotePitch pitch);

/**
 * The visible period of an SSG-EG loop at `pitch`, in ms; 0 when the patch is
 * not a looping mode at all, and infinite when it is one whose ramp never
 * finishes.
 *
 * Closed form, for the same reason the phase durations above are. The period
 * used to be measured -- a probe was run for twelve seconds and its folds
 * counted -- and twelve seconds was another horizon for the answer to change
 * across: a triangle at AR31 SL15 SR0 takes fifteen seconds per ramp at DR = 1,
 * so the probe saw no fold, reported no loop, and the graph fell back to the
 * policy for a patch that does not loop. One notch up at DR = 2 a fold landed
 * inside the window and the axis jumped from ten seconds to twenty-three.
 *
 * One ramp is the internal attenuation climbing from 0 to the fold at 0x200 --
 * through DR as far as the sustain level, then through SR the rest of the way,
 * all at SSG-EG's quadrupled increments -- plus the attack that follows each
 * fold. That attack starts from 0x200 rather than from silence, and at the
 * AR = 31 these modes are documented for it is instant. The alternating modes
 * (bit 1 of the SSG register) fold twice per visible period.
 *
 * Infinity is a phase of the ramp that never advances: DR = 0 below the
 * sustain level, SR = 0 above it, or AR = 0 after the fold. The envelope then
 * never folds again, which is not a slow loop but no loop -- exactly the case
 * the simulator names SsgNeverLoops -- and the graph is sized by the phase
 * that stalled instead.
 */
double ssg_loop_period_ms(const ym2612_eg::OperatorParams &op,
                          ym2612_eg::NotePitch pitch);

/**
 * How wide a given lifetime alone would ask the axis to be: a single smooth,
 * monotone, sub-linear map. This is the term that keeps a 30-second envelope
 * from swallowing the graph -- but on its own it can compress an envelope so
 * hard that a phase the user is editing falls off the right edge, which is what
 * window_for_timeline_ms() is for.
 */
double window_for_lifetime_ms(double lifetime_ms);

/**
 * The axis width the held envelope deserves: the compressed lifetime above, but
 * never narrower than the sustain's own start plus a readable slice of the
 * sustain. Clamped to the floor and ceiling in envelope_curve.cpp.
 *
 * The floor is why the compression can be as aggressive as it is. Compressing
 * the lifetime alone will happily draw a 6.4 s axis for a patch whose attack
 * takes 8.4 s, and then the decay and the sustain are both off the right edge
 * and the sliders that shape them look dead. Every phase the envelope has must
 * be at least partly on screen, so the axis reaches past the sustain's start
 * whatever the lifetime says; the lifetime only ever widens it further.
 */
double window_for_timeline_ms(const HeldTimeline &timeline);

/**
 * How much of the held envelope is worth *seeing*, in ms. An SSG loop is sized
 * from ssg_loop_period_ms() -- that is what the graph is about. Everything else
 * is held_timeline() put through window_for_timeline_ms().
 *
 * This is a scale, not a length: it is what the axis width is chosen from, and
 * the envelope itself is then drawn across the whole of that axis. Nothing
 * here is a key-off.
 */
double choose_held_ms(const ym2612_eg::OperatorParams &op);

/**
 * How long the release is simulated for: a generous ceiling of its own, so
 * nothing about the held envelope can change how far the release is measured.
 * Sampling stops the moment the envelope is at rest, so the ceiling only costs
 * anything for the release rates that genuinely run for seconds -- and RR = 0,
 * which never reaches silence at all.
 */
double release_max_ms();

/// The grid/label interval for a given span: a round number, 3-6 divisions.
double grid_step_ms(double span_ms);

/**
 * The one warning worth showing, or nullptr. Only one line is ever drawn, and
 * only one patch defect earns it: an SSG-EG mode driven by an attack rate the
 * hardware convention says should be 31. Everything else the simulator flags
 * is already visible in the shape of the curve.
 *
 * Two bits and a comparison, read straight off the registers -- which is all
 * the simulator's own SsgArBelow31 ever was. It used to be read back out of a
 * CurveResult, and that was the last reason a run existed whose points nothing
 * ever drew.
 */
const char *warning_line(const ym2612_eg::OperatorParams &op);

/// Everything the graph needs for one operator.
struct EnvelopeCurve {
  /// Attack, decay and sustain with the key never released: drawn as a line.
  ym2612_eg::CurveResult held;
  /// A release from full volume, starting at t = 0: drawn as a filled area.
  ym2612_eg::CurveResult release;

  /// The width of the time axis the curve was simulated for -- the target the
  /// drawing animates towards, not necessarily the width drawn this frame.
  double span_ms = 0.0;
  double held_ms = 0.0; ///< the window the axis width was chosen from
  double held_content_ms = 0.0;    ///< where the held polyline actually ends
  double release_content_ms = 0.0; ///< where the release polyline actually ends

  /// The held envelope came to rest -- an SR = 0 hold, a frozen attack, a
  /// sustain that reached silence -- so simulating it further would only
  /// repeat one level, and the trace is continued to the right edge flat.
  /// Everything else is simulated across the whole axis and needs no
  /// continuation at all; if one is ever needed anyway it follows the final
  /// slope, which is exact: every post-attack segment is linear in
  /// attenuation.
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
 * Two passes over the simulator, and both of them are drawn: a release from
 * full volume, which shares only the time axis with the other; and -- once the
 * window policy and the release have decided how wide the axis is -- the held
 * trace, simulated across the whole of it so a loop keeps looping to the right
 * edge.
 *
 * There used to be a third, a held-forever probe, and nothing it produced was
 * ever drawn: the axis was sized from what it saw and the warning read off
 * what it flagged. Both come from the registers now -- held_timeline() for the
 * phases, ssg_loop_period_ms() for a loop, warning_line() for the one line --
 * so the probe had nothing left to answer.
 *
 * A pure function of the operator and the reference note. It used to take the
 * span drawn last frame, because the axis was quantised onto a ladder with
 * hysteresis and so depended on where it had been. The ladder is gone -- the
 * axis is animated at draw time now, which does the same job better -- so there
 * is nothing left to remember.
 */
EnvelopeCurve build_envelope_curve(const ym2612::OperatorSettings &op);

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
