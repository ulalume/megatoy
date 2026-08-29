#include "gui/envelope/envelope_curve.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace ui::envelope {
namespace {

using ym2612_eg::CurveResult;
using ym2612_eg::CurveWarning;
using ym2612_eg::MarkerKind;
using ym2612_eg::OperatorParams;

// --------------------------------------------------------- held-window policy

/// How far the held-forever probe looks ahead. Long enough to catch the
/// interesting parks (an SR = 0 patch parks the moment its decay ends), short
/// enough that a slider drag can re-run it four times a frame. A sustain that
/// is still crawling towards silence after this simply reports no park, which
/// is the answer the window policy wants anyway.
constexpr double kProbeMs = 3000.0;
/// A looping SSG patch gets a longer probe: the loop period is what sizes the
/// graph, and sample_curve() stops after five periods anyway, so this only
/// costs anything for loops too slow to measure inside the shorter window.
constexpr double kSsgProbeMs = 12000.0;

/// A held window shorter than this reads as an accident rather than a sustain.
constexpr double kMinHeldMs = 50.0;
/// Above this the held part stops being informative -- the worked example's
/// sustain takes 27 s to reach silence, and drawing all of it would leave the
/// attack and decay a single pixel wide.
constexpr double kNominalMaxHeldMs = 2000.0;
/// ... except that the cap must never cut into the attack or the decay, so a
/// genuinely slow patch may exceed it. This is where even that gives up.
constexpr double kHardMaxHeldMs = 10000.0;
/// An SSG loop is what this graph exists for, so it is allowed more room than
/// a plain sustain before the periods start being dropped.
constexpr double kSsgMaxHeldMs = 5000.0;
/// Share of the held window reserved for the sustain segment, so SR always has
/// something to colour.
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
/// The release is never drawn wider than kReleaseSpanBudget * the held window
/// (that is what the budget above means), so there is nothing to gain from
/// simulating past it -- but a floor keeps the common release rates measuring
/// their true length rather than reporting the budget back.
constexpr double kMinReleaseMs = 4000.0;
/// ... and a ceiling, because RR = 0 never reaches silence at all. Sampling
/// stops the moment the envelope is at rest, so this only costs anything for
/// the handful of release rates that genuinely run for seconds.
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

/// Did the envelope come to rest at silence (nothing left to show) rather than
/// at a sustain level (a flat segment worth drawing)?
bool parked_silent(const CurveResult &probe) {
  return !probe.points.empty() &&
         probe.points.back().out >= ym2612_eg::kSilenceAttenuation;
}

// ------------------------------------------------------------- span quantiser

/**
 * Round spans, so the axis labels stay readable and -- more importantly -- so
 * a continuous parameter change lands on one of a handful of widths instead of
 * following the content pixel for pixel.
 */
constexpr double kSpanLadder[] = {25.0,   50.0,   100.0,  150.0, 250.0,
                                  500.0,  750.0,  1000.0, 1500.0, 2000.0,
                                  3000.0, 5000.0, 7500.0, 10000.0};
/// Grow once the content fills this much of the span ...
constexpr double kGrowAt = 0.95;
/// ... and shrink only once it drops below this much of it. The gap between
/// the two is the hysteresis: a curve that wanders inside it leaves the axis
/// alone.
constexpr double kShrinkAt = 0.40;

/// The narrowest ladder value the content fits inside.
double ladder_fit(double content_ms) {
  for (const double v : kSpanLadder) {
    if (content_ms <= kGrowAt * v) {
      return v;
    }
  }
  return kSpanLadder[std::size(kSpanLadder) - 1];
}

/// The ladder value a possibly-stale span corresponds to.
double ladder_snap(double span_ms) {
  for (const double v : kSpanLadder) {
    if (span_ms <= v) {
      return v;
    }
  }
  return kSpanLadder[std::size(kSpanLadder) - 1];
}

constexpr double kGridSteps[] = {5.0,    10.0,   25.0,   50.0,   100.0, 250.0,
                                 500.0,  1000.0, 2500.0, 5000.0, 10000.0};

} // namespace

ym2612_eg::NotePitch reference_pitch() {
  return ym2612_eg::NotePitch::from_midi(kReferenceMidiNote);
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

double release_max_ms(double held_ms) {
  return std::clamp(kReleaseSpanBudget * held_ms, kMinReleaseMs, kMaxReleaseMs);
}

/**
 * How much of the held envelope to draw, chosen from a run with the key held
 * forever so the decision can use what the envelope actually does rather than
 * a guess:
 *
 * - SSG looping: about 3.5 periods, which reads as a loop without turning into
 *   a hatch pattern. Audio-rate loops hit the floor below instead and show
 *   many more periods -- better than a graph that is all tail.
 * - Otherwise: long enough that the sustain owns a quarter of the width, so SR
 *   has something to highlight even when attack and decay are slow.
 * - The sustain starts where the decay ends -- or, for an SR = 0 patch, where
 *   the envelope parks, which is the same instant seen from the other side.
 * - An envelope that parks at *silence* has nothing left to show, so it is
 *   drawn just past that instead of a quarter longer again.
 * - The whole thing is capped at 2 s -- the 27 s sustain of the spec's worked
 *   example must not swallow the graph -- but never below the attack + decay,
 *   which would misplace the sustain instead of merely shortening it.
 *
 * This is the width the axis is sized from, not the length of the trace:
 * nothing about the envelope changes at this instant, and the trace itself is
 * simulated across the whole axis the width turns into.
 */
double choose_held_ms(const CurveResult &probe, const OperatorParams &op) {
  double held = 0.0;
  double ceiling = kHardMaxHeldMs;
  double floor_ms = kMinHeldMs;

  const double period = loop_period_ms(probe, op);
  if (period > 0.0) {
    held = kSsgLoopPeriods * period;
    ceiling = kSsgMaxHeldMs;
    // A loop carries its own time scale. Widening an audio-rate loop to
    // kMinHeldMs would pack it into a solid block of cycles instead of showing
    // its shape, so the periods are the floor.
    floor_ms = std::min(kMinHeldMs, held);
  } else {
    const double attack_end = first_marker_ms(probe, MarkerKind::AttackEnd);
    const double decay_end = first_marker_ms(probe, MarkerKind::DecayEnd);
    const bool silent = parked_silent(probe);
    double sustain_start = std::max({decay_end, attack_end, 0.0});
    if (std::isfinite(probe.park_ms) && !silent) {
      sustain_start = std::max(sustain_start, probe.park_ms);
    }

    const double with_sustain = sustain_start / (1.0 - kSustainShare);
    held = with_sustain;
    if (std::isfinite(probe.park_ms) && silent) {
      held = std::max(held, probe.park_ms * 1.05);
    }
    ceiling = std::clamp(std::max(kNominalMaxHeldMs, with_sustain), kMinHeldMs,
                         kHardMaxHeldMs);
  }

  return std::clamp(held, floor_ms, ceiling);
}

double quantize_span_ms(double content_ms, double current_span_ms) {
  const double content = std::max(content_ms, 0.0);
  if (!(current_span_ms > 0.0)) {
    return ladder_fit(content);
  }
  const double span = ladder_snap(current_span_ms);
  if (content > kGrowAt * span || content < kShrinkAt * span) {
    // Both directions land on the same value, which is what keeps a content
    // length sitting between the two thresholds from oscillating: ladder_fit()
    // is idempotent, so re-running the test on its own result changes nothing.
    return ladder_fit(content);
  }
  return span;
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

EnvelopeCurve build_envelope_curve(const ym2612::OperatorSettings &op,
                                   double previous_span_ms) {
  EnvelopeCurve out;

  const OperatorParams params = to_operator_params(op);
  const ym2612_eg::NotePitch pitch = reference_pitch();

  // 1. Held forever, generously: discovers park time, loop frequency and
  //    warnings. The probe sees the whole loop and the whole hold, so it stays
  //    the authority on both even though only part of it is drawn.
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
  release.max_ms = release_max_ms(out.held_ms);
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
  const double budget = loops ? kSsgSpanBudget : kReleaseSpanBudget;
  double content = std::max(out.held_ms, out.release_content_ms);
  if (out.held_ms > 0.0) {
    content = std::min(content, out.held_ms * budget);
  }
  out.span_ms = quantize_span_ms(content, previous_span_ms);

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
  curve_ = build_envelope_curve(op, curve_.span_ms);
  params_ = params;
  pitch_ = pitch;
  valid_ = true;
  ++rebuilds_;
  return curve_;
}

} // namespace ui::envelope
