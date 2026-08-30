#include "gui/envelope/envelope_curve.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <limits>

namespace ui::envelope {
namespace {

using ym2612_eg::CurveResult;
using ym2612_eg::MarkerKind;
using ym2612_eg::OperatorParams;

// --------------------------------------------------------- held-window policy

/// The scale the axis is pulled towards: an envelope that lives this long is
/// drawn at exactly this width. It is about the length of an ordinary attack
/// and decay, which is what the graph is usually being read for.
constexpr double kWindowRefMs = 400.0;
/// ... and how far it is allowed to travel from there. Sub-linear, so a
/// lifetime ten times longer widens the axis about three times rather than
/// ten: 0.5 makes the window exactly the geometric mean of the lifetime and
/// kWindowRefMs. That lands a 300 ms envelope on a 346 ms axis, a 3 s one on
/// 1.1 s and a 30 s one on 3.5 s -- each still readable, and each still a
/// visibly different width from its neighbours.
constexpr double kWindowExponent = 0.5;
/// The longest life the compression models. Beyond it every envelope is "far
/// longer than the axis" and the floor decides the width instead.
constexpr double kMaxModelledLifeMs = 6400.0;
/// The share of the axis kept past the sustain's start, so that the sustain --
/// and with it the slider that shapes it -- is always partly on screen however
/// hard the lifetime above is compressed.
constexpr double kSustainShare = 0.25;
/// Roughly this many SSG loop periods are drawn.
constexpr double kSsgLoopPeriods = 3.5;
/// How much of a looping graph the release may claim. Past this the loop the
/// release is compared against stops being readable, so the axis keeps the
/// loop's scale and the release runs off the right edge instead.
constexpr double kSsgSpanBudget = 2.0;
/// The same bargain for a plain patch. A release is often far longer than the
/// held envelope -- RR = 2 takes over ten seconds where the attack and decay
/// take three hundred milliseconds -- and letting it set the width alone would
/// squeeze the part the user is editing into the left margin. Past this the
/// release simply runs off the right edge, which still reads as "very long".
constexpr double kReleaseSpanBudget = 4.0;
/// How long a release is simulated for. RR = 0 never reaches silence at all,
/// so there has to be a ceiling; sampling stops the moment the envelope is at
/// rest, so it only costs anything for the handful of release rates that
/// genuinely run for seconds.
constexpr double kMaxReleaseMs = 10000.0;

/// The Silence marker as a time, or infinity when the trace never gets there.
double silence_or_never(const CurveResult &curve) {
  const double ms = first_marker_ms(curve, MarkerKind::Silence);
  return ms >= 0.0 ? ms : std::numeric_limits<double>::infinity();
}

/// The slope the trace would be continued along past its last point, in
/// attenuation units per ms. Zero when the envelope came to rest, and zero
/// when the whole trace sits at one level: both are continued flat.
double tail_slope(const CurveResult &curve, bool at_rest) {
  const auto &points = curve.points;
  if (at_rest || points.size() < 2) {
    return 0.0;
  }
  const ym2612_eg::CurvePoint &last = points.back();
  // sample_curve() closes every polyline with a point at the end of the
  // simulated span, which can repeat the level already there -- a final edge
  // that is both very short and perfectly flat. Extrapolating a whole graph
  // width from that would draw a flat line across an envelope that is plainly
  // still moving, so measure from the last point at a different level instead.
  std::size_t base = points.size() - 1;
  while (base > 0 && points[base - 1].out == last.out) {
    --base;
  }
  if (base == 0) {
    return 0.0; // the whole trace sits at one level
  }
  const ym2612_eg::CurvePoint &previous = points[base - 1];
  const double dt = static_cast<double>(last.ms) - previous.ms;
  return dt > 0.0 ? (static_cast<double>(last.out) - previous.out) / dt : 0.0;
}

// ------------------------------------------------------------------ the axis

constexpr double kGridSteps[] = {5.0,    10.0,   25.0,   50.0,   100.0, 250.0,
                                 500.0,  1000.0, 2500.0, 5000.0, 10000.0};

/// The reference note, and the only mutable state in this file. The app writes
/// it whenever the preference changes; every curve built afterwards is drawn
/// at it, and EnvelopeCurveCache notices because it remembers the pitch it
/// last built at.
int g_reference_midi_note = kDefaultReferenceMidiNote;

} // namespace

void set_reference_midi_note(int midi_note) {
  g_reference_midi_note =
      std::clamp(midi_note, kMinReferenceMidiNote, kMaxReferenceMidiNote);
}

int reference_midi_note() { return g_reference_midi_note; }

double first_marker_ms(const CurveResult &curve, MarkerKind kind) {
  for (const ym2612_eg::Marker &m : curve.markers) {
    if (m.kind == kind) {
      return m.ms;
    }
  }
  return -1.0;
}

ym2612_eg::NotePitch reference_pitch() {
  return ym2612_eg::NotePitch::from_midi(g_reference_midi_note);
}

uint8_t packed_ssg(const ym2612::OperatorSettings &op) {
  // Byte for byte what ym2612::Operator::write_settings() puts in $90-$9F.
  // The shape bits are kept even while the enable bit is clear, exactly as the
  // chip receives them; with bit3 down they are inert on hardware and in the
  // simulator alike.
  return static_cast<uint8_t>((op.ssg_enable ? 0x08 : 0x00) |
                              (op.ssg_type_envelope_control & 0x07));
}

ym2612_eg::OperatorParams
to_operator_params(const ym2612::OperatorSettings &op) {
  OperatorParams params;
  params.ar = static_cast<uint8_t>(op.attack_rate & 0x1F);
  params.dr = static_cast<uint8_t>(op.decay_rate & 0x1F);
  params.sr = static_cast<uint8_t>(op.sustain_rate & 0x1F);
  params.rr = static_cast<uint8_t>(op.release_rate & 0x0F);
  params.sl = static_cast<uint8_t>(op.sustain_level & 0x0F);
  params.tl = static_cast<uint8_t>(op.total_level & 0x7F);
  params.ks = static_cast<uint8_t>(op.key_scale & 0x03);
  params.ssg = packed_ssg(op);
  return params;
}

bool same_envelope(const OperatorParams &lhs, const OperatorParams &rhs) {
  return lhs.ar == rhs.ar && lhs.dr == rhs.dr && lhs.sr == rhs.sr &&
         lhs.rr == rhs.rr && lhs.sl == rhs.sl && lhs.tl == rhs.tl &&
         lhs.ks == rhs.ks && lhs.ssg == rhs.ssg;
}

/// The release is simulated on its own terms: sampling stops at silence, so a
/// generous ceiling costs nothing on a fast one, and tying it to the held
/// window would let AR and DR change how long a release is drawn.
double release_max_ms() { return kMaxReleaseMs; }

double drawable_sustain_start_ms(const ym2612_eg::PhaseDurations &phases) {
  // Capped at the ceiling rather than at kMaxModelledLifeMs: this figure is
  // what keeps a phase on the graph, so shortening it below what the axis
  // could actually have shown would hide the very phase being edited. A phase
  // that never ends saturates here too, which is why "no decay" and "a decay
  // measured in minutes" both take the widest axis -- they draw the same flat
  // line either way.
  return std::min(phases.attack_ms, kMaxHeldMs) +
         std::min(phases.decay_ms, kMaxHeldMs);
}

double drawable_lifetime_ms(const ym2612_eg::PhaseDurations &phases) {
  // The lifetime only widens the axis, so it saturates sooner: past a few
  // seconds "how long until silence" stops telling a reader anything the shape
  // does not, and letting it run to the ceiling buries a 300 ms decay under ten
  // seconds of flat line.
  return std::min(phases.attack_ms, kMaxModelledLifeMs) +
         std::min(phases.decay_ms, kMaxModelledLifeMs) +
         std::min(phases.sustain_ms, kMaxModelledLifeMs);
}

double window_for_lifetime_ms(double lifetime_ms) {
  if (!(lifetime_ms > 0.0)) {
    return kMinHeldMs;
  }
  const double window =
      kWindowRefMs * std::pow(lifetime_ms / kWindowRefMs, kWindowExponent);
  return std::clamp(window, kMinHeldMs, kMaxHeldMs);
}

double window_for_timeline_ms(const ym2612_eg::PhaseDurations &phases) {
  // The axis has to reach past the sustain's start whatever the compression
  // says, or the last phase of the envelope -- and the slider that shapes it --
  // is simply not on the graph. An 8.4 s attack compressed on its lifetime
  // alone asks for a 6.4 s axis, which cuts the attack off before it finishes
  // and takes the decay and the sustain with it.
  //
  // The share is of the *window*, so the sustain always owns a quarter of it:
  // sustain_start / (1 - share) is the width at which that is true. This is the
  // one guarantee the marker-driven policy did get right; what was wrong was
  // asking a probe where the sustain started.
  const double floor_ms =
      drawable_sustain_start_ms(phases) / (1.0 - kSustainShare);
  // No floor of its own: window_for_lifetime_ms() never returns less than
  // kMinHeldMs, and the sustain floor only ever raises.
  const double compressed = window_for_lifetime_ms(drawable_lifetime_ms(phases));
  return std::min(std::max(compressed, floor_ms), kMaxHeldMs);
}

/**
 * How much of the held envelope to draw.
 *
 * An SSG loop is sized from its period -- about 3.5 of them, which reads as a
 * loop without turning into a hatch pattern. Everything else is the envelope's
 * own phase durations put through window_for_timeline_ms().
 *
 * Both are closed forms over the registers, and that is the point: the width
 * is a continuous function of every rate, with no horizon for a slow envelope
 * to cross and no marker that has to have been seen for SL or SR to matter.
 *
 * This is the width the axis is sized from, not the length of the trace:
 * nothing about the envelope changes at this instant, and the trace itself is
 * simulated across the whole axis the width turns into.
 */
double choose_held_ms(const OperatorParams &op) {
  return choose_held_ms(op, reference_pitch());
}

double choose_held_ms(const OperatorParams &op, ym2612_eg::NotePitch pitch) {
  const double period = ym2612_eg::ssg_loop_period_ms(op, pitch);
  // An infinite period is a ramp with a phase that never advances, which is
  // not a slow loop but no loop: the fold never comes, and what the graph has
  // to show is the phase that stalled. The policy below is the right one for
  // it, exactly as it is for a patch with SSG-EG switched off.
  if (period > 0.0 && std::isfinite(period)) {
    const double held = kSsgLoopPeriods * period;
    // A loop carries its own time scale. Widening an audio-rate loop to
    // kMinHeldMs would pack it into a solid block of cycles instead of showing
    // its shape, so the periods are the floor.
    //
    // The ceiling gives way for a slow loop rather than the other way round: a
    // graph of a loop that cannot fit one period of it shows nothing about the
    // loop, and for these patches the loop is the whole content.
    const double ceiling =
        std::min(std::max(kMaxHeldMs, kLoopVisiblePeriods * period),
                 kLoopMaxAxisMs);
    return std::clamp(held, std::min(kMinHeldMs, held), ceiling);
  }
  return window_for_timeline_ms(ym2612_eg::phase_durations(op, pitch));
}

double grid_step_ms(double span_ms) {
  for (const double step : kGridSteps) {
    if (span_ms <= step * 6.0) {
      return step;
    }
  }
  return kGridSteps[std::size(kGridSteps) - 1];
}

const char *warning_line(const CurveResult &curve) {
  // Only one line is ever drawn, and only one defect earns it. The others the
  // library reports are things the graph already shows -- a frozen attack is a
  // flat line at silence, a loop that never loops has no teeth, an audio-rate
  // loop is a solid block of them -- so naming them as well only crowds the
  // picture. This is a ladder with one rung: putting a line back is a rung,
  // ordered most broken first, and CurveWarning carries them all.
  for (const ym2612_eg::CurveWarning w : curve.warnings) {
    if (w == ym2612_eg::CurveWarning::SsgArBelow31) {
      return "AR<31: non-standard SSG-EG";
    }
  }
  return nullptr;
}

EnvelopeCurve build_envelope_curve(const ym2612::OperatorSettings &op) {
  return build_envelope_curve(op, reference_pitch());
}

EnvelopeCurve build_envelope_curve(const ym2612::OperatorSettings &op,
                                   ym2612_eg::NotePitch pitch,
                                   double min_span_ms) {
  EnvelopeCurve out;

  const OperatorParams params = to_operator_params(op);

  // 1. What the axis has to hold, from the registers alone. Nothing is
  //    simulated to find it: the library answers every phase of the held
  //    envelope in closed form, and an SSG loop's period with it.
  const double period_ms = ym2612_eg::ssg_loop_period_ms(params, pitch);
  const bool loops = period_ms > 0.0 && std::isfinite(period_ms);
  out.held_ms = choose_held_ms(params, pitch);

  // 2. The release, on its own: keyed on at full volume and released on sample
  //    zero. gate_ms = 0 routes it through the chip's real key-off rules --
  //    the SSG inversion latch, the 4x increments, the hard cut at 0x200 --
  //    which is why an SSG-EG patch's release is so much shorter than the same
  //    patch without it. It shares nothing with the held trace but the axis.
  //
  //    "Full volume" is the library's loudest_attenuation(), not 0: with an
  //    inverted SSG-EG mode 0 is the quiet end of the ramp, and releasing from
  //    there is a one-sample cut rather than a curve. AR is zeroed for this run
  //    alone -- sample_curve() calls key_on(), which snaps an instant attack
  //    straight to att = 0 and would throw the start level away, and the
  //    release rate does not depend on AR. The AttackFrozen warning that comes
  //    back with it is never read: warning_line() is asked about the registers,
  //    not about any run.
  ym2612_eg::CurveRequest release;
  release.op = params;
  release.op.ar = 0;
  release.pitch = pitch;
  release.gate_ms = 0.0;
  release.max_ms = release_max_ms();
  release.start_att = ym2612_eg::loudest_attenuation(params);
  out.release = ym2612_eg::sample_curve(release);
  out.release_content_ms =
      out.release.points.empty()
          ? 0.0
          : static_cast<double>(out.release.points.back().ms);
  out.release_truncated =
      out.release_content_ms >= release.max_ms * 0.999 &&
      (out.release.points.empty() ||
       out.release.points.back().out < ym2612_eg::kMaxAttenuation);

  // 3. The axis has to hold both traces -- but neither may crush the other. A
  //    loop keeps its own scale, and a release much longer than the held
  //    envelope is allowed to run off the right edge rather than flatten the
  //    part being edited.
  //
  //    The width is simply the content it has to hold, at full precision: a
  //    slider drag does not make the axis breathe because the drawing animates
  //    towards it, not because the answer has been rounded off.
  const double budget = loops ? kSsgSpanBudget : kReleaseSpanBudget;
  double content = std::max(out.held_ms, out.release_content_ms);
  if (out.held_ms > 0.0) {
    content = std::min(content, out.held_ms * budget);
  }
  out.span_ms = std::max(content, kMinSpanMs);

  // 4. The held trace itself, simulated across the WHOLE axis rather than only
  //    as far as the window policy asked for. Still no key-off, so SR = 0
  //    holds flat and SR > 0 shows its real slow decay rather than a level
  //    some invented gate stopped it at.
  //
  //    A looping patch is the reason this runs to the edge instead of being
  //    extrapolated there: a sawtooth continued along the slope of its last
  //    ramp is not a sawtooth. But gate_ms < 0 means more to sample_curve()
  //    than "never released" -- it also licenses it to stop as soon as there
  //    is nothing new to see, which for a loop is after five periods, leaving
  //    the trace in mid-air. So a loop is given a key-off just past the end of
  //    the window instead: none of it falls inside the graph, and no early exit
  //    either. Everything else keeps the held-forever run, whose early exit is
  //    at the park -- where the envelope really has come to rest and the flat
  //    tail below is exact.
  ym2612_eg::CurveRequest request;
  request.op = params;
  request.pitch = pitch;
  // min_span_ms is the axis this curve will actually be drawn on, which for
  // a voice overlay is the reference curve's rather than its own.
  request.max_ms = std::max({out.span_ms, out.held_ms, min_span_ms});
  request.gate_ms = loops ? request.max_ms + 1.0 : -1.0;
  out.held = ym2612_eg::sample_curve(request);
  // A finite park is exactly "the trace ended because there was nothing left
  // to draw" -- continue it flat rather than along a slope of zero noise.
  out.held_parked = std::isfinite(out.held.park_ms);

  out.attack_end_ms = first_marker_ms(out.held, MarkerKind::AttackEnd);
  out.decay_end_ms = first_marker_ms(out.held, MarkerKind::DecayEnd);

  // The markers come back sorted by time, so the folds do too.
  for (const ym2612_eg::Marker &m : out.held.markers) {
    if (m.kind == MarkerKind::SsgFold) {
      out.ssg_folds.push_back(m.ms);
    }
  }

  // The library only raises Silence once the trace has come to rest, which is
  // the question a fading cursor asks: is this voice finished, or just quiet on
  // its way somewhere?
  out.held_silence_ms = silence_or_never(out.held);
  out.release_silence_ms = silence_or_never(out.release);

  out.held_tail_slope = tail_slope(out.held, out.held_parked);
  out.release_tail_slope = tail_slope(out.release, !out.release_truncated);

  const int tl_att = static_cast<int>(params.tl) * 8;
  out.peak_out = static_cast<uint16_t>(
      std::min(tl_att, static_cast<int>(ym2612_eg::kMaxAttenuation)));
  // The level the decay aims at, in the same output units as the curve.
  out.sustain_out = static_cast<uint16_t>(
      std::min(ym2612_eg::sustain_attenuation(params.sl) + tl_att,
               static_cast<int>(ym2612_eg::kMaxAttenuation)));

  out.warning = warning_line(out.held);
  return out;
}

// ------------------------------------------------------- the live cursor

double curve_out_at_ms(const ym2612_eg::CurveResult &curve, double ms) {
  const auto &points = curve.points;
  if (points.empty()) {
    return static_cast<double>(ym2612_eg::kMaxAttenuation);
  }
  if (!(ms > points.front().ms)) {
    return points.front().out;
  }
  if (ms >= points.back().ms) {
    return points.back().out;
  }
  // Binary search rather than a walk: an SSG trace reduced to one bucket per
  // slot carries thousands of points, and this runs once per voice per
  // operator per frame.
  std::size_t lo = 0;
  std::size_t hi = points.size() - 1;
  while (hi - lo > 1) {
    const std::size_t mid = lo + (hi - lo) / 2;
    if (static_cast<double>(points[mid].ms) <= ms) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  const double t0 = points[lo].ms;
  const double t1 = points[hi].ms;
  const double dt = t1 - t0;
  if (!(dt > 0.0)) {
    return points[hi].out;
  }
  const double u = (ms - t0) / dt;
  return points[lo].out +
         (static_cast<double>(points[hi].out) - points[lo].out) * u;
}

double release_entry_ms(const CurveResult &release, double out) {
  const auto &points = release.points;
  if (points.empty()) {
    return 0.0;
  }
  if (out <= points.front().out) {
    return points.front().ms;
  }
  // A linear scan, because a release trace is a straight ramp in attenuation
  // and RDP decimates it to a handful of vertices. Interpolating inside the
  // crossing edge is therefore exact rather than approximate: the polyline IS
  // the line.
  for (std::size_t i = 1; i < points.size(); ++i) {
    const double a0 = points[i - 1].out;
    const double a1 = points[i].out;
    if (a1 < out) {
      continue;
    }
    if (a1 <= a0) {
      return points[i].ms; // a step, not a ramp: it arrives at this instant
    }
    const double u = std::clamp((out - a0) / (a1 - a0), 0.0, 1.0);
    return points[i - 1].ms +
           (static_cast<double>(points[i].ms) - points[i - 1].ms) * u;
  }
  return points.back().ms;
}

/// Fold the elapsed time into the loop the axis draws.
///
/// Returns `elapsed_ms` unchanged unless the held trace loops and the cursor
/// has run past the last whole period on the axis. The first fold anchors the
/// phase: everything before it is the attack the loop only performs once.
double wrapped_into_loop_ms(const EnvelopeCurve &curve, double elapsed_ms,
                            double axis_span_ms) {
  if (!(curve.held.loop_hz > 0.0) || !(axis_span_ms > 0.0) ||
      curve.ssg_folds.empty()) {
    return elapsed_ms;
  }
  // The folds are in order, so the last one on the axis is a search rather
  // than a sweep of every marker the curve carries.
  const auto past_edge =
      std::upper_bound(curve.ssg_folds.begin(), curve.ssg_folds.end(),
                       axis_span_ms,
                       [](double edge, float fold) { return edge < fold; });
  if (past_edge == curve.ssg_folds.begin()) {
    return elapsed_ms; // not even the first fold is on the axis
  }
  const double first_fold = curve.ssg_folds.front();
  const double last_fold = *(past_edge - 1);
  // Two folds bound at least one whole period; one bounds none.
  const double window = last_fold - first_fold;
  if (!(window > 0.0) || elapsed_ms <= last_fold) {
    return elapsed_ms;
  }
  return first_fold + std::fmod(elapsed_ms - first_fold, window);
}

VoiceCursor cursor_for_voice(const EnvelopeCurve &curve, double since_key_on_ms,
                             double since_key_off_ms, double axis_span_ms) {
  VoiceCursor cursor;
  cursor.released = since_key_off_ms >= 0.0;

  // Where the held trace comes to rest, if it does. Past this instant nothing
  // about the envelope changes, so neither does the cursor -- an SR = 0 patch
  // parks at its sustain level and waits for the key to come up.
  const double park_ms = curve.held.park_ms;
  const double held_ms = std::max(since_key_on_ms, 0.0);

  double on_trace_ms = 0.0;
  double silence_ms = 0.0;
  if (!cursor.released) {
    on_trace_ms = std::min(held_ms, park_ms);
    // How far the note has actually been: measured before the wrap folds a
    // loop's elapsed time back onto the axis, so a loop past its first pass
    // has been through the whole of it.
    cursor.held_to_ms = on_trace_ms;
    silence_ms = curve.held_silence_ms;
  } else {
    const double released_for = std::max(since_key_off_ms, 0.0);
    // The level the voice actually had when the key came up -- park-clamped
    // for the same reason as above.
    const double at_key_off_ms =
        std::min(std::max(held_ms - released_for, 0.0), park_ms);
    // The AUDIBLE level, not the internal one: on key-off an inverted SSG-EG
    // mode latches what was being heard into the attenuation, so a release
    // continues from the level the ear was on. Matching the internal value
    // would enter the trace wherever that number happens to sit -- which for
    // an inverted mode is somewhere else entirely.
    const double out = curve_out_at_ms(curve.held, at_key_off_ms);
    // The release from that level is not a new curve: it is the drawn one,
    // entered at the point where it is already at that level.
    cursor.release_from_ms = release_entry_ms(curve.release, out);
    cursor.release_origin_ms = at_key_off_ms;
    // The held part stops growing the moment the key comes up, however long
    // the release runs on after it.
    cursor.held_to_ms = at_key_off_ms;
    // The release is drawn from where the note actually let go, so the cursor
    // carries on from where it is rather than jumping to wherever that level
    // sits on a release that began at full volume.
    on_trace_ms = at_key_off_ms + released_for;
    // Silence is a property of the release, so it moves with it.
    silence_ms = std::isfinite(curve.release_silence_ms)
                     ? at_key_off_ms + curve.release_silence_ms -
                           cursor.release_from_ms
                     : curve.release_silence_ms;
  }

  if (!cursor.released) {
    // A loop never parks -- the note goes round and round for as long as it is
    // held -- so the cursor goes round with it rather than stopping at the
    // right-hand edge while the sound carries on. It wraps over the whole
    // periods the axis holds, so it sweeps the drawn cycles and starts again.
    on_trace_ms = wrapped_into_loop_ms(curve, on_trace_ms, axis_span_ms);
  }

  // Measured before the axis clamp, so a voice whose cursor is parked at the
  // right-hand edge still fades out when the envelope beneath it dies.
  cursor.silent_for_ms =
      std::isfinite(silence_ms) ? std::max(0.0, on_trace_ms - silence_ms) : 0.0;
  // Still moving when it reaches the right-hand end of the axis: it carries on
  // past it and stops being drawn. Parking it on the edge would say the
  // envelope had come to rest there, which it has not.
  cursor.ms = std::max(on_trace_ms, 0.0);
  cursor.held_to_ms =
      std::clamp(cursor.held_to_ms, 0.0, std::max(axis_span_ms, 0.0));
  return cursor;
}

const EnvelopeCurve *VoiceCurveCache::get(const ym2612::OperatorSettings &op,
                                          ym2612_eg::NotePitch pitch,
                                          const EnvelopeCurve &reference,
                                          int &build_budget) {
  const ym2612_eg::OperatorParams params = to_operator_params(op);
  const ym2612_eg::NotePitch ref_pitch = reference_pitch();
  const bool same_context = valid_ && same_envelope(params, params_) &&
                            ref_pitch.fnum == reference_pitch_.fnum &&
                            ref_pitch.block == reference_pitch_.block &&
                            reference_span_ms_ == reference.span_ms;
  if (!same_context) {
    // Everything here was built for registers or an axis that no longer
    // exist, so the lot goes.
    entries_ = {};
    used_ = 0;
    params_ = params;
    reference_pitch_ = ref_pitch;
    reference_span_ms_ = reference.span_ms;
    valid_ = true;
  }

  const int ksv = ym2612_eg::key_scale_value(params, pitch);
  // The note reaches the envelope only through ksv, so a voice that shares the
  // reference note's is drawn by the curve already on screen. With KS = 0 --
  // what most patches use -- that is a whole two octaves either side of the
  // reference, which is why a note-on usually costs nothing at all.
  if (ksv == ym2612_eg::key_scale_value(params, ref_pitch)) {
    return &reference;
  }

  ++clock_;
  for (std::size_t i = 0; i < used_; ++i) {
    if (entries_[i].ksv == ksv) {
      entries_[i].last_used = clock_;
      return &entries_[i].curve;
    }
  }

  if (build_budget <= 0) {
    return nullptr; // next frame
  }
  --build_budget;

  std::size_t slot = used_;
  if (used_ < kMaxEntries) {
    ++used_;
  } else {
    // Six voices, six entries: this only fires when the sounding notes have
    // moved on, and then the entry going out is one nothing is playing.
    slot = 0;
    for (std::size_t i = 1; i < used_; ++i) {
      if (entries_[i].last_used < entries_[slot].last_used) {
        slot = i;
      }
    }
  }
  entries_[slot].ksv = ksv;
  entries_[slot].last_used = clock_;
  entries_[slot].curve = build_envelope_curve(op, pitch, reference.span_ms);
  ++rebuilds_;
  return &entries_[slot].curve;
}

// ------------------------------------------------- how often to rebuild

double RebuildThrottle::interval_for_ms(double cost_ms) {
  if (!(cost_ms > 0.0)) {
    return 0.0;
  }
  // The budget is a share of wall-clock time, so the interval is the cost
  // divided by that share: a rebuild worth one budget's work every frame is
  // spaced one frame apart, and one worth six is spaced six.
  const double interval =
      cost_ms * (kRebuildBudgetPeriodMs / kRebuildBudgetMs);
  return std::min(interval, kMaxRebuildDeferMs);
}

bool RebuildThrottle::may_rebuild(double now_ms) const {
  if (!ever_) {
    return true;
  }
  return now_ms - last_rebuild_ms_ >= interval_ms();
}

void RebuildThrottle::note_rebuild(double now_ms, double cost_ms) {
  last_rebuild_ms_ = now_ms;
  // A clock that went backwards, or a rebuild too quick to measure, is worth
  // nothing to the spacing: it earns no wait at all.
  last_cost_ms_ = std::max(cost_ms, 0.0);
  ever_ = true;
}

double steady_now_ms() {
  const auto since_epoch = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration<double, std::milli>(since_epoch).count();
}

const EnvelopeCurve &EnvelopeCurveCache::get(const ym2612::OperatorSettings &op) {
  const ym2612_eg::OperatorParams params = to_operator_params(op);
  const ym2612_eg::NotePitch pitch = reference_pitch();
  const auto same_pitch = [](ym2612_eg::NotePitch lhs,
                             ym2612_eg::NotePitch rhs) {
    return lhs.fnum == rhs.fnum && lhs.block == rhs.block;
  };

  // Whether this is the second frame in a row to ask for the same thing --
  // measured against the previous REQUEST rather than against the curve, so a
  // value that arrived while a rebuild was deferred still counts as settled.
  const bool settled = requested_valid_ && same_envelope(params, requested_) &&
                       same_pitch(pitch, requested_pitch_);
  requested_ = params;
  requested_pitch_ = pitch;
  requested_valid_ = true;

  if (valid_ && same_envelope(params, params_) && same_pitch(pitch, pitch_)) {
    // The curve on hand is the one being asked for: nothing to decide, and no
    // clock read either. This is every frame the user is not editing.
    return curve_;
  }

  const double now_ms = now_ms_();
  if (valid_ && !settled && !throttle_.may_rebuild(now_ms)) {
    // Still moving, and the last rebuild has not earned its keep yet. Last
    // frame's curve goes out again -- one frame further out of date than the
    // one the graph drew a moment ago, which is a difference no drag can see.
    return curve_;
  }

  curve_ = build_envelope_curve(op);
  params_ = params;
  pitch_ = pitch;
  valid_ = true;
  ++rebuilds_;
  throttle_.note_rebuild(now_ms, now_ms_() - now_ms);
  return curve_;
}

} // namespace ui::envelope
