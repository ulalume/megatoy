#pragma once

/**
 * One operator's registers turned into two drawable, millisecond-accurate
 * traces.
 *
 * The shapes come from ym2612_eg, and so do the numbers behind them: how long
 * each phase takes and how fast an SSG loop runs, in closed form. This file is
 * the policy around those answers -- which note to draw at, how wide the time
 * axis should be, where a sounding voice's cursor sits, and which single
 * warning is worth a line of text.
 *
 * The two traces are independent, on one elapsed-time axis. Chaining them
 * would mean inventing a key-off instant, which cuts the sustain short at an
 * arbitrary time and then starts the release from whatever level that fiction
 * produced. So the held envelope is simulated with the key never released,
 * which is the only way SR reads truthfully (SR = 0 holds flat, SR > 0
 * crawls), and the release is simulated on its own from full volume.
 *
 * Free of ImGui so the policy can be unit-tested on its own
 * (tests/gui/envelope_curve_test.cpp); the drawing lives in
 * gui/components/envelope_image.cpp.
 */

#include "ym2612/types.hpp"

#include <ym2612_eg/ym2612_eg.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace ui::envelope {

/**
 * The single reference note every envelope graph is drawn at, and the only
 * place the note is decided: everything downstream asks reference_pitch(),
 * and the app pushes the preference in through set_reference_midi_note().
 *
 * A setting the user changes deliberately rather than something the playing
 * moves: the graph shows a shape, not a performance, and one that followed the
 * keyboard would rescale itself under the user's hands while they edit.
 */
inline constexpr int kDefaultReferenceMidiNote = 60; // middle C
/// C0 to B7: megatoy's own note range, one F-num block each. Above B7 the
/// chip has no block left and every note folds onto the same pitch.
inline constexpr int kMinReferenceMidiNote = 12;  // C0
inline constexpr int kMaxReferenceMidiNote = 107; // B7

/**
 * How wide the time axis may get, in ms. The one home for the question.
 *
 * kMinSpanMs is the narrowest axis worth drawing whatever the content says; a
 * held window narrower than kMinHeldMs reads as an accident rather than a
 * sustain. kMaxHeldMs is the widest an ordinary envelope ever gets -- only the
 * ones that never finish at all reach it, since the compression needs a
 * lifetime of four minutes to arrive there on its own.
 *
 * A loop is the one thing allowed past that ceiling, because a graph of a loop
 * that cannot fit one period shows nothing about the loop. kLoopVisiblePeriods
 * of a period stays on the axis however slow it is, up to kLoopMaxAxisMs.
 */
inline constexpr double kMinSpanMs = 25.0;
inline constexpr double kMinHeldMs = 50.0;
inline constexpr double kMaxHeldMs = 10000.0;
inline constexpr double kLoopVisiblePeriods = 1.2;
inline constexpr double kLoopMaxAxisMs = 20000.0;

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

/// The first marker of `kind` on a trace, in ms, or negative when it has none.
double first_marker_ms(const ym2612_eg::CurveResult &curve,
                       ym2612_eg::MarkerKind kind);

/// Whether two register sets draw the same curve. Every field of the envelope,
/// so the caches rebuild on a change of any of them and on nothing else --
/// multiple, detune and the rest do not shape an envelope.
bool same_envelope(const ym2612_eg::OperatorParams &lhs,
                   const ym2612_eg::OperatorParams &rhs);

/**
 * ym2612_eg::phase_durations(), saturated at the longest phase an axis can
 * usefully hold. A drawing policy, not a fact about the envelope: "never
 * decays" and "decays over a minute" look the same on any axis, and saturating
 * keeps them beside each other instead of a cliff apart.
 *
 * They saturate at different lengths. The sustain's start is what keeps a
 * phase on the graph, so it saturates at the ceiling itself; the lifetime only
 * ever widens the axis, so it saturates at kMaxModelledLifeMs, past which
 * "how long until silence" would only bury a 300 ms decay under ten seconds of
 * flat line.
 */
double drawable_sustain_start_ms(const ym2612_eg::PhaseDurations &phases);
double drawable_lifetime_ms(const ym2612_eg::PhaseDurations &phases);

/**
 * How wide a given lifetime alone would ask the axis to be: one smooth,
 * monotone, sub-linear map. The term that keeps a 30-second envelope from
 * swallowing the graph -- and, on its own, the one that can compress a phase
 * off the right edge, which window_for_timeline_ms() is there to stop.
 */
double window_for_lifetime_ms(double lifetime_ms);

/**
 * The axis width the held envelope deserves: the compressed lifetime above,
 * but never narrower than the sustain's own start plus a readable slice of the
 * sustain, and never wider than kMaxHeldMs.
 *
 * The floor is why the compression can be as aggressive as it is. Compressing
 * the lifetime alone hands a patch whose attack takes 8.4 s a 6.4 s axis, and
 * then the decay and the sustain are both off the right edge and the sliders
 * that shape them look dead.
 *
 * It is a floor up to the ceiling and no further. kSustainShare of the axis is
 * kept for the sustain, so the floor is sustain_start / (1 - share), which
 * passes kMaxHeldMs once the attack and decay together run past about 7.5 s.
 * Beyond that the ceiling wins and the sustain really is off the right edge:
 * an envelope whose first two phases outlast the widest axis there is cannot
 * be shown whole.
 */
double window_for_timeline_ms(const ym2612_eg::PhaseDurations &phases);

/**
 * How much of the held envelope is worth seeing, in ms: an SSG loop from
 * ssg_loop_period_ms(), everything else from phase_durations() through
 * window_for_timeline_ms().
 *
 * A scale, not a length. The axis width is chosen from it and the envelope is
 * then drawn across the whole of that axis; nothing here is a key-off.
 */
double choose_held_ms(const ym2612_eg::OperatorParams &op);
double choose_held_ms(const ym2612_eg::OperatorParams &op,
                      ym2612_eg::NotePitch pitch);

/**
 * How long the release is simulated for: a ceiling of its own, so nothing
 * about the held envelope can change how far the release is measured. Sampling
 * stops the moment the envelope is at rest, so it only costs anything for the
 * rates that genuinely run for seconds -- and RR = 0, which never rests.
 */
double release_max_ms();

/// The grid/label interval for a given span: a round number, 3-6 divisions.
double grid_step_ms(double span_ms);

/**
 * The one warning worth showing, or nullptr. Only one patch defect earns a
 * line: an SSG-EG mode driven by an attack rate the hardware convention says
 * should be 31. Everything else the simulator flags is already visible in the
 * shape of the curve. Two bits and a comparison off the registers, which is
 * all the simulator's own SsgArBelow31 is.
 */
const char *warning_line(const ym2612_eg::CurveResult &curve);

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
  /// Where the release polyline actually ends, which is where it reached
  /// silence unless its budget ran out first; the axis is sized from it.
  double release_content_ms = 0.0;

  /// The held envelope came to rest -- an SR = 0 hold, a frozen attack, a
  /// sustain that reached silence -- so simulating it further would only
  /// repeat one level.
  bool held_parked = false;
  /// The release was still falling when its budget ran out.
  bool release_truncated = false;

  /// The slope, in attenuation units per ms, a trace that stops before the
  /// right-hand edge is continued along -- zero when it came to rest, which
  /// continues it flat. A slope is exact rather than a guess: every
  /// post-attack segment is linear in attenuation. It is a property of the
  /// curve, so it is measured once here rather than by scanning back from the
  /// last vertex every frame.
  double held_tail_slope = 0.0;
  double release_tail_slope = 0.0;

  /// The first instant each trace is at or below the hardware mute floor and
  /// stays there -- the library's own Silence marker, lifted out so a live
  /// cursor does not rescan the markers every frame. Infinite when the trace
  /// never gets there.
  double held_silence_ms = std::numeric_limits<double>::infinity();
  double release_silence_ms = std::numeric_limits<double>::infinity();

  // Segment boundaries on the held trace, ms; negative when the segment never
  // happened.
  double attack_end_ms = -1.0;
  double decay_end_ms = -1.0;

  /// Every instant the held trace folds, in order -- an SSG-EG loop's teeth.
  /// Lifted out of the markers because a live cursor needs the first and last
  /// fold inside the axis on every voice of every operator of every frame, and
  /// a loop dense enough to saturate the library's marker ceiling carries four
  /// thousand of them.
  std::vector<float> ssg_folds;

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
 * Nothing else is simulated. How long each phase takes, how fast a loop runs
 * and which warning the patch earns are all closed forms over the registers:
 * phase_durations(), ssg_loop_period_ms(), warning_line().
 *
 * A pure function of the operator and the reference note. The axis moving
 * smoothly is the drawing's job, not this one's.
 */
EnvelopeCurve build_envelope_curve(const ym2612::OperatorSettings &op);

/**
 * The same curve at an arbitrary note, for a voice that is actually sounding.
 *
 * `min_span_ms` is the axis the curve will be DRAWN on -- the reference
 * curve's, not its own -- and the held trace is simulated across at least that
 * much. Otherwise an overlay would have to be extrapolated onto the part of
 * the axis its own window policy did not reach, and a sawtooth continued along
 * the slope of its last ramp is not a sawtooth.
 */
EnvelopeCurve build_envelope_curve(const ym2612::OperatorSettings &op,
                                   ym2612_eg::NotePitch pitch,
                                   double min_span_ms = 0.0);

// ------------------------------------------------------- the live cursor

/// The polyline's internal attenuation at `ms`, linearly interpolated and
/// clamped to the ends. Binary search: the held trace can carry thousands of
/// points and this runs per voice per operator per frame.
double curve_att_at_ms(const ym2612_eg::CurveResult &curve, double ms);

/**
 * Where on a release trace a release from attenuation `att` begins.
 *
 * The release is linear in attenuation, so a note let go at level L follows
 * precisely the trace that is already on screen -- entered later. Finding that
 * entry point is therefore exact, and there is no second release curve to
 * build per voice: the first instant the trace reaches `att` IS where the
 * voice joins it.
 *
 * Takes the trace rather than the whole curve so the tests can ask the same
 * question of a release the simulator really ran, and compare the two answers
 * without a second copy of the interpolation to disagree with.
 */
double release_entry_ms(const ym2612_eg::CurveResult &release, double att);

/// Where a sounding voice is on its own envelope.
struct VoiceCursor {
  /// x on the graph's time axis, in ms. Past the end of the axis for a voice
  /// that has outrun it -- such a cursor is not drawn rather than being pinned
  /// to the edge.
  double ms = 0.0;
  /// The voice is past key-off and riding the release trace.
  bool released = false;
  /// How long the voice has been inaudible; 0 while it can still be heard.
  /// The graph fades a voice out over kVoiceFadeMs of this.
  double silent_for_ms = 0.0;
  /// Where on the release trace this voice's release begins -- the point at
  /// which the drawn release is already at the level the key came up on.
  /// Negative while the key is still down.
  double release_from_ms = -1.0;
  /// Where on the graph that release is drawn from: the instant the key came
  /// up, on the held trace's own time axis. The release keeps the shape it has
  /// on the release trace but is drawn from here, so the cursor carries
  /// straight on instead of jumping across the graph. Negative while held.
  double release_origin_ms = -1.0;
  /// How much of the held trace this voice has actually been through: a note
  /// draws the road it has travelled, not the road ahead. It only ever grows,
  /// and stops growing at key-off -- a loop that has already come round once
  /// has been through all of it, so it keeps the whole thing.
  double held_to_ms = 0.0;
};

/**
 * The cursor for one voice, given how long ago its key went down and (if it
 * has) come up. `since_key_off_ms` is negative while the key is still held.
 *
 * Two rules decide everything:
 *
 *   - the cursor advances only while the envelope is CHANGING, so an SR = 0
 *     patch's cursor waits at the park rather than sliding along a flat line;
 *   - on key-off it moves to the release trace, at the point where that trace
 *     is already at the level the voice actually had, and advances from there.
 */
VoiceCursor cursor_for_voice(const EnvelopeCurve &curve, double since_key_on_ms,
                             double since_key_off_ms, double axis_span_ms);

/**
 * The curves of the notes being played, keyed on (registers, ksv) rather than
 * on the note: an operator's envelope depends on the note ONLY through
 * `ksv = keycode >> (3 - KS)`, so with KS = 0 the whole keyboard has four
 * distinct entries and a chord inside one octave shares a single one.
 *
 * At most six entries, because at most six voices can sound; the least
 * recently asked-for is evicted. Entries are held by value in a fixed array,
 * so a reference handed out stays valid across later get() calls.
 */
class VoiceCurveCache {
public:
  static constexpr std::size_t kMaxEntries = 6;

  /**
   * The curve for `op` at `pitch`, drawn on `reference`'s axis.
   *
   * Returns `reference` itself when the two share a key-scale value, and the
   * cached entry when there is one -- neither costs a simulation, which is
   * why a note-on is usually free. The whole cache is dropped when the
   * registers or the reference note change, because every entry would have
   * been stale anyway.
   *
   * When a curve does have to be built, one is taken out of `build_budget`;
   * with none left this returns nullptr and the caller leaves that voice for
   * the next frame. Simulating the slowest envelope the chip can produce takes
   * about twelve milliseconds, so an arpeggio across four operators would
   * otherwise drop several frames at once. A voice that waits a frame or two
   * for its curve is invisible; a stutter is not.
   */
  const EnvelopeCurve *get(const ym2612::OperatorSettings &op,
                           ym2612_eg::NotePitch pitch,
                           const EnvelopeCurve &reference, int &build_budget);

  /// Curves actually simulated, and entries held. Only the tests read these,
  /// and only they can: whether a cache caches is not visible in its answers.
  int rebuild_count() const { return rebuilds_; }
  std::size_t size() const { return used_; }

private:
  struct Entry {
    int ksv = -1;
    uint64_t last_used = 0;
    EnvelopeCurve curve;
  };

  ym2612_eg::OperatorParams params_{};
  ym2612_eg::NotePitch reference_pitch_{};
  double reference_span_ms_ = 0.0;
  bool valid_ = false;
  std::array<Entry, kMaxEntries> entries_{};
  std::size_t used_ = 0;
  uint64_t clock_ = 0;
  int rebuilds_ = 0;
};

// ------------------------------------------------- how often to rebuild

/**
 * How much of a frame one operator's graph may spend rebuilding its curve,
 * and the frame that budget is per.
 *
 * A rebuild costs whatever the patch costs to simulate, and the patches are
 * not close to each other. Over a sweep of 18 900 of them the median curve
 * takes 0.7 ms and the slowest fifteen, and the two populations are separated
 * by an obvious valley: 288 patches land between 1.5 and 2.0 ms against 3 991
 * below 0.1 ms and 2 577 between 3.5 and 4.0. An ordinary ADSR patch measures
 * 0.9-1.1 ms and an SSG-EG one 4.0 ms, one on each side of it.
 *
 * The budget is that valley, so the common case is not throttled at all -- it
 * would have to become half again as slow before it were. Four operators can
 * be dragged at once, so four rebuilds can land on one frame: the budget is
 * per operator, and four of them is 6 ms of a 16.7 ms frame. That is the
 * ceiling the whole graph is held to while a value is moving, and an ordinary
 * patch -- 3.6 ms a frame for four operators -- is already inside it.
 */
inline constexpr double kRebuildBudgetMs = 1.5;
inline constexpr double kRebuildBudgetPeriodMs = 1000.0 / 60.0;
/**
 * ... and the longest the graph may lag the registers however expensive the
 * curve is. 13.5 ms of work is where the budget's own spacing reaches this,
 * and only a handful of patches in the sweep cost that much: below it the
 * ceiling never binds at all, and above it the graph would stop looking slow
 * and start looking frozen.
 */
inline constexpr double kMaxRebuildDeferMs = 150.0;

/**
 * Whether a rebuild is allowed to happen yet, from what the last one cost.
 *
 * A rebuild that fits the budget is simply done every frame -- the interval it
 * earns is shorter than a frame, so the test never refuses one. An expensive
 * one is spaced out in proportion to what it costs, which is the whole policy:
 * a curve that takes k times the budget waits k frames, so every patch spends
 * the same share of the machine however slow it is to simulate. Nothing here
 * knows about ImGui, or about a frame; it is told the time and what the work
 * cost, and answers when the next one may run.
 */
class RebuildThrottle {
public:
  /// How long a rebuild costing `cost_ms` earns itself before the next one.
  static double interval_for_ms(double cost_ms);

  /// Whether a rebuild may run at `now_ms`. True until one has been recorded:
  /// the first curve is never deferred, because there is nothing to draw
  /// instead of it.
  bool may_rebuild(double now_ms) const;

  /// One rebuild, at `now_ms`, that took `cost_ms`.
  void note_rebuild(double now_ms, double cost_ms);

  double last_cost_ms() const { return last_cost_ms_; }
  double interval_ms() const { return interval_for_ms(last_cost_ms_); }

private:
  double last_rebuild_ms_ = 0.0;
  double last_cost_ms_ = 0.0;
  bool ever_ = false;
};

/// A monotonic clock in milliseconds -- what the throttle runs on unless a
/// test hands it another one. Deliberately not ImGui's: this file is free of
/// it, and the throttle measures work rather than frames.
double steady_now_ms();

/**
 * Remembers one operator's curve and rebuilds it only when the registers that
 * shape it (or the reference note) actually change -- the same
 * compare-then-recompute pattern PatchSession uses for audio settings.
 *
 * A drag changes them every frame, though, and then the compare says "rebuild"
 * every frame too. For an ordinary patch that is what should happen and what
 * still does. For the expensive ones it is not affordable, so a rebuild the
 * throttle has not licensed yet is skipped and last frame's curve drawn again
 * -- exactly what the graph already shows between frames, and what the cursors
 * and the voice ghosts are already drawn against.
 *
 * The one thing that must never be deferred is the value the drag ends on. So
 * a request the cache saw on the previous frame as well is built whatever the
 * throttle says: a value that has stopped moving is the user's answer, and it
 * is on screen exactly one frame later.
 */
class EnvelopeCurveCache {
public:
  const EnvelopeCurve &get(const ym2612::OperatorSettings &op);

  /// The clock the throttle runs on. Only the tests replace it, and they have
  /// to: a rebuild's cost is the difference between a read taken when the
  /// cache is asked and one taken immediately afterwards, so a fake clock is
  /// the only way to say what a rebuild costs.
  using Clock = double (*)();
  void set_clock(Clock clock) { now_ms_ = clock; }

  /// Curves actually simulated. Only the tests read it; see VoiceCurveCache.
  int rebuild_count() const { return rebuilds_; }

private:
  ym2612_eg::OperatorParams params_{};
  ym2612_eg::NotePitch pitch_{};
  /// What the previous frame asked for, which is not always what was built:
  /// a request that repeats is a value that has settled.
  ym2612_eg::OperatorParams requested_{};
  ym2612_eg::NotePitch requested_pitch_{};
  bool requested_valid_ = false;
  EnvelopeCurve curve_;
  bool valid_ = false;
  int rebuilds_ = 0;
  RebuildThrottle throttle_;
  Clock now_ms_ = &steady_now_ms;
};

} // namespace ui::envelope
