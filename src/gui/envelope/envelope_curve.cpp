#include "gui/envelope/envelope_curve.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace ui::envelope {
namespace {

using ym2612_eg::CurveResult;
using ym2612_eg::CurveWarning;
using ym2612_eg::MarkerKind;
using ym2612_eg::OperatorParams;

// --------------------------------------------------------- held-window policy

/// How far the held-forever probe looks ahead. The probe no longer decides how
/// wide the axis is -- that is a closed form now, precisely so that a horizon
/// cannot decide anything -- so this only has to be long enough to measure a
/// loop and raise the warnings, and short enough that a slider drag can re-run
/// it four times a frame.
constexpr double kProbeMs = 3000.0;
/// A looping SSG patch gets a longer probe: the loop period is what sizes the
/// graph, and sample_curve() stops after five periods anyway, so this only
/// costs anything for loops too slow to measure inside the shorter window.
constexpr double kSsgProbeMs = 12000.0;

/// A held window shorter than this reads as an accident rather than a sustain.
constexpr double kMinHeldMs = 50.0;
/// The widest the axis ever gets. Only the envelopes that never finish at all
/// -- SR = 0, DR = 0, AR = 0 -- reach it; the curve below needs a lifetime of
/// four minutes to arrive here on its own.
constexpr double kHardMaxHeldMs = 10000.0;
/// An SSG loop is what this graph exists for, so it is allowed more room than
/// a plain sustain before the periods start being dropped.
constexpr double kSsgMaxHeldMs = 5000.0;
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

double first_marker_ms(const CurveResult &curve, MarkerKind kind) {
  for (const ym2612_eg::Marker &m : curve.markers) {
    if (m.kind == kind) {
      return m.ms;
    }
  }
  return -1.0;
}

bool has_warning(const CurveResult &curve, CurveWarning w) {
  return std::find(curve.warnings.begin(), curve.warnings.end(), w) !=
         curve.warnings.end();
}

bool ssg_loops(const OperatorParams &op) {
  // Enabled and hold clear: the ramp restarts instead of latching.
  return (op.ssg & 0x08) != 0 && (op.ssg & 0x01) == 0;
}

/**
 * The visible loop period.
 *
 * sample_curve() only publishes loop_hz once it has seen three folds, which a
 * slow loop cannot manage inside the probe window, so fall back to the fold
 * markers themselves. The alternating modes fold twice per visible period.
 */
double loop_period_ms(const CurveResult &probe, const OperatorParams &op) {
  if (!ssg_loops(op)) {
    return 0.0;
  }
  if (probe.loop_hz > 0.0) {
    return 1000.0 / probe.loop_hz;
  }
  const double ramps_per_period = (op.ssg & 0x02) != 0 ? 2.0 : 1.0;
  double first = -1.0;
  for (const ym2612_eg::Marker &m : probe.markers) {
    if (m.kind != MarkerKind::SsgFold) {
      continue;
    }
    if (first < 0.0) {
      first = m.ms;
      continue;
    }
    // Two folds is a measured ramp; prefer it over the estimate below.
    return (m.ms - first) * ramps_per_period;
  }
  // One fold: the first ramp also carries the attack, which is short enough
  // next to a loop slow enough to land here.
  return first > 0.0 ? first * ramps_per_period : 0.0;
}

// ------------------------------------------------- the held envelope's clock

/// The attenuation one EG tick adds, on average, at this effective rate.
///
/// increment_at() indexes kIncTable with three bits of the free-running EG
/// counter taken from above rate_shift(), so across one turn of those bits each
/// of the eight entries lands exactly once and their mean is the true gain per
/// tick -- the shift being how many ticks each entry is held for. Rows 0 and 1
/// are all zero, which is the chip's way of saying this phase never advances.
double atten_per_eg_tick(int rate, bool ssg) {
  int sum = 0;
  for (int i = 0; i < 8; ++i) {
    sum += ym2612_eg::detail::kIncTable[rate][i];
  }
  const double mean = static_cast<double>(sum) / 8.0;
  const double per_tick =
      mean / static_cast<double>(1 << ym2612_eg::detail::rate_shift(rate));
  // SSG-EG quadruples every post-attack increment.
  return ssg ? per_tick * 4.0 : per_tick;
}

/// How long a phase that is linear in attenuation takes to climb from
/// `from_att` to `to_att`, in ms. Decay and sustain both are: they add a fixed
/// increment per tick, so the duration is one division rather than a
/// simulation. A rate that never advances takes forever, and says so.
double linear_phase_ms(int rate, int from_att, int to_att, bool ssg,
                       double eg_hz) {
  if (to_att <= from_att) {
    return 0.0;
  }
  const double per_tick = atten_per_eg_tick(rate, ssg);
  if (!(per_tick > 0.0)) {
    return std::numeric_limits<double>::infinity();
  }
  const double ticks = static_cast<double>(to_att - from_att) / per_tick;
  return ticks * 1000.0 / eg_hz;
}

/// How long the attack takes, in ms.
///
/// The attack is the one phase that is not linear: it multiplies what is left,
/// `att += (~att * inc) >> 4`, so there is no closed form and the recurrence is
/// run instead. It converges from 0x3FF in a couple of hundred table slots
/// whatever the rate, which is nothing next to the hundreds of thousands of
/// samples the same stretch costs to simulate.
double attack_ms(int rate, double eg_hz) {
  // key_on() snaps these straight to att = 0; eg_step() guards on `rate < 62`.
  if (rate >= 62) {
    return 0.0;
  }
  int sum = 0;
  for (int i = 0; i < 8; ++i) {
    sum += ym2612_eg::detail::kIncTable[rate][i];
  }
  if (sum == 0) {
    // Rates 0 and 1, i.e. AR = 0: the attack never finishes, so neither does
    // anything after it.
    return std::numeric_limits<double>::infinity();
  }
  const int shift = ym2612_eg::detail::rate_shift(rate);
  int att = static_cast<int>(ym2612_eg::kMaxAttenuation);
  long long slots = 0;
  // The slowest table row alternates {0, 1}, which needs about 214 slots from
  // 0x3FF; the bound is only here so a future table could not hang a frame.
  constexpr long long kSlotLimit = 4096;
  for (; att > 0 && slots < kSlotLimit; ++slots) {
    const int inc = ym2612_eg::detail::kIncTable[rate][slots & 7];
    if (inc != 0) {
      // Arithmetic shift of a negative value, exactly as the simulator does it.
      att += (~att * inc) >> 4;
    }
  }
  return static_cast<double>(slots << shift) * 1000.0 / eg_hz;
}

// ------------------------------------------------------------------ the axis

/// The narrowest axis worth drawing, whatever the content says. A loop fast
/// enough to want less than this is drawn as a band of cycles either way.
constexpr double kMinSpanMs = 25.0;

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

uint16_t loudest_attenuation(const OperatorParams &op) {
  // output() inverts while SSG-EG is enabled and J = attack XOR invert is set;
  // on key-on the invert flag is clear, so the attack bit alone decides.
  const bool inverted = (op.ssg & 0x08) != 0 && (op.ssg & 0x04) != 0;
  return inverted ? ym2612_eg::kSsgFoldAttenuation : uint16_t{0};
}

bool same_envelope(const OperatorParams &lhs, const OperatorParams &rhs) {
  return lhs.ar == rhs.ar && lhs.dr == rhs.dr && lhs.sr == rhs.sr &&
         lhs.rr == rhs.rr && lhs.sl == rhs.sl && lhs.tl == rhs.tl &&
         lhs.ks == rhs.ks && lhs.ssg == rhs.ssg;
}

double probe_max_ms(const OperatorParams &op) {
  return ssg_loops(op) ? kSsgProbeMs : kProbeMs;
}

/// The release is simulated on its own terms: sampling stops at silence, so a
/// generous ceiling costs nothing on a fast one, and tying it to the held
/// window would let AR and DR change how long a release is drawn.
double release_max_ms() { return kMaxReleaseMs; }

HeldTimeline held_timeline(const OperatorParams &op,
                           ym2612_eg::NotePitch pitch) {
  // The graph is drawn at one note and the chip's rates are keyed to it, so the
  // key-scale value is part of every duration below.
  const int ksv = pitch.keycode() >> (3 - (op.ks & 3));
  const bool ssg = (op.ssg & 0x08) != 0;
  // Everything the graph draws is at the Genesis' own clock, which is what
  // CurveRequest defaults to as well.
  const double eg_hz = ym2612_eg::eg_rate_hz(ym2612_eg::kNtscClockHz);

  // Where the held envelope runs out of scale. Without SSG-EG the chip cuts the
  // output dead the moment the attenuation reaches 0x3F0; with it, both the
  // fold and the hold latch happen at 0x200 instead.
  const int end_att = ssg ? static_cast<int>(ym2612_eg::kSsgFoldAttenuation)
                          : 0x3F0;
  const int sustain_att =
      std::min(ym2612_eg::detail::sustain_attenuation(op.sl), end_att);

  const int ar = ym2612_eg::detail::effective_rate(op.ar & 0x1F, ksv);
  const int dr = ym2612_eg::detail::effective_rate(op.dr & 0x1F, ksv);
  const int sr = ym2612_eg::detail::effective_rate(op.sr & 0x1F, ksv);

  HeldTimeline timeline;
  timeline.attack_ms = attack_ms(ar, eg_hz);
  timeline.decay_ms = linear_phase_ms(dr, 0, sustain_att, ssg, eg_hz);
  timeline.sustain_ms = linear_phase_ms(sr, sustain_att, end_att, ssg, eg_hz);
  return timeline;
}

double held_lifetime_ms(const OperatorParams &op, ym2612_eg::NotePitch pitch) {
  // Infinity is contagious, which is right: a phase that never ends means the
  // ones after it never start.
  return held_timeline(op, pitch).lifetime_ms();
}

double window_for_lifetime_ms(double lifetime_ms) {
  if (!(lifetime_ms > 0.0)) {
    return kMinHeldMs;
  }
  if (!std::isfinite(lifetime_ms)) {
    return kHardMaxHeldMs;
  }
  const double window =
      kWindowRefMs * std::pow(lifetime_ms / kWindowRefMs, kWindowExponent);
  return std::clamp(window, kMinHeldMs, kHardMaxHeldMs);
}

double window_for_timeline_ms(const HeldTimeline &timeline) {
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
  const double floor_ms = timeline.sustain_start_ms() / (1.0 - kSustainShare);
  const double compressed = window_for_lifetime_ms(timeline.lifetime_ms());
  return std::clamp(std::max(compressed, floor_ms), kMinHeldMs,
                    kHardMaxHeldMs);
}

/**
 * How much of the held envelope to draw.
 *
 * An SSG loop is sized from its measured period -- about 3.5 of them, which
 * reads as a loop without turning into a hatch pattern. The period is the one
 * thing here the probe still decides, because a loop's time scale is its own
 * and nothing about the registers predicts it as cheaply.
 *
 * Everything else is the envelope's own phase durations, computed rather than
 * observed, and turned into a width by window_for_timeline_ms(). No test for
 * whether the envelope parked, whether it reached silence, or whether a marker
 * exists. The version that asked those questions asked them of a probe that
 * stopped after three seconds, so an envelope slower than the horizon reported
 * no decay at all -- SL stopped mattering, SR stopped mattering, and DR jumped
 * threefold as it crossed. A closed form has no horizon to cross.
 *
 * This is the width the axis is sized from, not the length of the trace:
 * nothing about the envelope changes at this instant, and the trace itself is
 * simulated across the whole axis the width turns into.
 */
double choose_held_ms(const CurveResult &probe, const OperatorParams &op) {
  const double period = loop_period_ms(probe, op);
  if (period > 0.0) {
    const double held = kSsgLoopPeriods * period;
    // A loop carries its own time scale. Widening an audio-rate loop to
    // kMinHeldMs would pack it into a solid block of cycles instead of showing
    // its shape, so the periods are the floor.
    return std::clamp(held, std::min(kMinHeldMs, held), kSsgMaxHeldMs);
  }
  return window_for_timeline_ms(held_timeline(op, reference_pitch()));
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
  // ordered most broken first, and CurveWarning still carries them all.
  if (has_warning(curve, CurveWarning::SsgArBelow31)) {
    return "AR<31: non-standard SSG-EG";
  }
  return nullptr;
}

EnvelopeCurve build_envelope_curve(const ym2612::OperatorSettings &op) {
  EnvelopeCurve out;

  const OperatorParams params = to_operator_params(op);
  const ym2612_eg::NotePitch pitch = reference_pitch();

  // 1. Held forever, generously: discovers the loop frequency and the
  //    warnings. It is no longer asked how long any phase lasts -- a probe can
  //    only report what it saw before its horizon, and that horizon was the
  //    axis' whole discontinuity.
  ym2612_eg::CurveRequest request;
  request.op = params;
  request.pitch = pitch;
  request.gate_ms = -1.0;
  request.max_ms = probe_max_ms(params);
  const CurveResult probe = ym2612_eg::sample_curve(request);

  out.held_ms = choose_held_ms(probe, params);
  const double period_ms = loop_period_ms(probe, params);
  const bool loops = period_ms > 0.0;

  // 2. The release, on its own: keyed on at full volume and released on sample
  //    zero. gate_ms = 0 routes it through the chip's real key-off rules --
  //    the SSG inversion latch, the 4x increments, the hard cut at 0x200 --
  //    which is why an SSG-EG patch's release is so much shorter than the same
  //    patch without it. It shares nothing with the held trace but the axis.
  //
  //    "Full volume" is loudest_attenuation(), not 0: with an inverted SSG-EG
  //    mode 0 is the quiet end of the ramp, and releasing from there is a
  //    one-sample cut rather than a curve. AR is zeroed for this run alone --
  //    sample_curve() calls key_on(), which snaps an instant attack straight
  //    to att = 0 and would throw the start level away, and the release rate
  //    does not depend on AR. The AttackFrozen warning that comes back with it
  //    is never read: warning_line() is asked about the probe, not this run.
  ym2612_eg::CurveRequest release;
  release.op = params;
  release.op.ar = 0;
  release.pitch = pitch;
  release.gate_ms = 0.0;
  release.max_ms = release_max_ms();
  release.start_att = loudest_attenuation(params);
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
  //    The width is simply the content it has to hold. It used to be rounded
  //    onto a ladder of a dozen values with hysteresis, so that a slider drag
  //    could not make the axis breathe; the drawing animates the axis now,
  //    which stops the breathing without also throwing away the answer -- the
  //    ladder was turning a two percent change of content into a thirty
  //    percent change of axis, and back.
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
  //    is nothing new to see, which for a loop is after five periods, and that
  //    is exactly the trace that stopped in mid-air. So a measured loop is
  //    given a key-off just past the end of the window instead: none of it
  //    falls inside the graph, and no early exit either. Everything else keeps
  //    the held-forever run, whose early exit is at the park -- where the
  //    envelope really has come to rest and the flat tail below is exact.
  request.max_ms = std::max(out.span_ms, out.held_ms);
  request.gate_ms = loops ? request.max_ms + 1.0 : -1.0;
  out.held = ym2612_eg::sample_curve(request);
  out.held_content_ms = out.held.points.empty()
                            ? 0.0
                            : static_cast<double>(out.held.points.back().ms);
  // A finite park is exactly "the trace ended because there was nothing left
  // to draw" -- continue it flat rather than along a slope of zero noise.
  out.held_parked = std::isfinite(out.held.park_ms);

  out.attack_end_ms = first_marker_ms(out.held, MarkerKind::AttackEnd);
  out.decay_end_ms = first_marker_ms(out.held, MarkerKind::DecayEnd);

  const int tl_att = static_cast<int>(params.tl) * 8;
  out.peak_out = static_cast<uint16_t>(
      std::min(tl_att, static_cast<int>(ym2612_eg::kMaxAttenuation)));
  // The level the decay aims at, in the same output units as the curve.
  out.sustain_out = static_cast<uint16_t>(
      std::min(ym2612_eg::detail::sustain_attenuation(params.sl) + tl_att,
               static_cast<int>(ym2612_eg::kMaxAttenuation)));

  out.warning = warning_line(probe);
  return out;
}

const EnvelopeCurve &EnvelopeCurveCache::get(const ym2612::OperatorSettings &op) {
  const ym2612_eg::OperatorParams params = to_operator_params(op);
  const ym2612_eg::NotePitch pitch = reference_pitch();
  const bool unchanged = valid_ && same_envelope(params, params_) &&
                         pitch.fnum == pitch_.fnum && pitch.block == pitch_.block;
  if (unchanged) {
    return curve_;
  }
  curve_ = build_envelope_curve(op);
  params_ = params;
  pitch_ = pitch;
  valid_ = true;
  ++rebuilds_;
  return curve_;
}

} // namespace ui::envelope
