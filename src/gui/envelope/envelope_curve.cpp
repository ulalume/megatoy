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

// ---------------------------------------------------------------- gate policy

/// How far the held-forever probe looks ahead. Long enough to catch the
/// interesting parks (an SR = 0 patch parks the moment its decay ends), short
/// enough that a slider drag can re-run it four times a frame. A sustain that
/// is still crawling towards silence after this simply reports no park, which
/// is the answer the gate policy wants anyway.
constexpr double kProbeMs = 3000.0;
/// A looping SSG patch gets a longer probe: the loop period is what sizes the
/// graph, and sample_curve() stops after five periods anyway, so this only
/// costs anything for loops too slow to measure inside the shorter window.
constexpr double kSsgProbeMs = 12000.0;

/// A held note shorter than this reads as an accident rather than a sustain.
constexpr double kMinGateMs = 50.0;
/// Above this the held part stops being informative -- the worked example's
/// sustain takes 27 s to reach silence, and drawing all of it would leave the
/// attack and decay a single pixel wide.
constexpr double kNominalMaxGateMs = 2000.0;
/// ... except that the cap must never cut into the attack or the decay, so a
/// genuinely slow patch may exceed it. This is where even that gives up.
constexpr double kHardMaxGateMs = 10000.0;
/// An SSG loop is what this graph exists for, so it is allowed more room than
/// a plain sustain before the periods start being dropped.
constexpr double kSsgMaxGateMs = 5000.0;
/// Share of the held time reserved for the sustain segment, so SR always has
/// something to colour.
constexpr double kSustainShare = 0.25;
/// Roughly this many SSG loop periods before the key comes up.
constexpr double kSsgLoopPeriods = 3.5;

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

bool same_envelope(const OperatorParams &lhs, const OperatorParams &rhs) {
  return lhs.ar == rhs.ar && lhs.dr == rhs.dr && lhs.sr == rhs.sr &&
         lhs.rr == rhs.rr && lhs.sl == rhs.sl && lhs.tl == rhs.tl &&
         lhs.ks == rhs.ks && lhs.ssg == rhs.ssg;
}

double probe_max_ms(const OperatorParams &op) {
  return ssg_loops(op) ? kSsgProbeMs : kProbeMs;
}

/**
 * The gate is chosen from a run with the key held forever, so the decision can
 * use what the envelope actually does rather than a guess:
 *
 * - SSG looping: about 3.5 periods, which reads as a loop without turning into
 *   a hatch pattern. Audio-rate loops hit the floor below instead and show
 *   many more periods -- better than a graph that is all release.
 * - Otherwise: long enough that the sustain owns a quarter of the held time,
 *   so SR has something to highlight even when attack and decay are slow.
 * - The sustain starts where the decay ends -- or, for an SR = 0 patch, where
 *   the envelope parks, which is the same instant seen from the other side.
 * - An envelope that parks at *silence* has nothing left to show, so it is
 *   held just past that instead of a quarter longer again.
 * - The whole thing is capped at 2 s -- the 27 s sustain of the spec's worked
 *   example must not swallow the graph -- but never below the attack + decay,
 *   which would misplace the sustain instead of merely shortening it.
 *
 * The release budget is generous but bounded; sample_curve() stops as soon as
 * the envelope is at rest, so the budget only costs anything for the patches
 * that genuinely release slowly, and those get drawn truncated at the edge.
 */
Gate choose_gate(const CurveResult &probe, const OperatorParams &op) {
  double gate = 0.0;
  double ceiling = kHardMaxGateMs;

  const double period = loop_period_ms(probe, op);
  if (period > 0.0) {
    gate = kSsgLoopPeriods * period;
    ceiling = kSsgMaxGateMs;
  } else {
    const double attack_end = first_marker_ms(probe, MarkerKind::AttackEnd);
    const double decay_end = first_marker_ms(probe, MarkerKind::DecayEnd);
    const bool silent = parked_silent(probe);
    double sustain_start = std::max({decay_end, attack_end, 0.0});
    if (std::isfinite(probe.park_ms) && !silent) {
      sustain_start = std::max(sustain_start, probe.park_ms);
    }

    const double with_sustain = sustain_start / (1.0 - kSustainShare);
    gate = with_sustain;
    if (std::isfinite(probe.park_ms) && silent) {
      gate = std::max(gate, probe.park_ms * 1.05);
    }
    ceiling = std::clamp(std::max(kNominalMaxGateMs, with_sustain), kMinGateMs,
                         kHardMaxGateMs);
  }

  gate = std::clamp(gate, kMinGateMs, ceiling);

  const double release_budget = std::clamp(3.0 * gate, 200.0, 4000.0);
  return Gate{gate, gate + release_budget};
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

const char *warning_line(const CurveResult &curve, const OperatorParams &op) {
  // Most broken first: only one line is ever drawn.
  if (has_warning(curve, CurveWarning::AttackFrozen)) {
    return "AR=0: never sounds";
  }
  if (has_warning(curve, CurveWarning::SsgArBelow31)) {
    return "AR<31: unintended SSG behavior";
  }
  if (has_warning(curve, CurveWarning::SsgNeverLoops)) {
    return op.sr == 0 ? "never loops (SR=0)" : "never loops (DR=0)";
  }
  if (has_warning(curve, CurveWarning::SsgAudioRate)) {
    return "audio-rate - adds sidebands";
  }
  return nullptr;
}

EnvelopeCurve build_envelope_curve(const ym2612::OperatorSettings &op,
                                   double previous_span_ms) {
  EnvelopeCurve out;

  ym2612_eg::CurveRequest request;
  request.op = to_operator_params(op);
  request.pitch = reference_pitch();
  request.gate_ms = -1.0; // held forever: discover park / loop / warnings
  request.max_ms = probe_max_ms(request.op);
  const CurveResult probe = ym2612_eg::sample_curve(request);

  const Gate gate = choose_gate(probe, request.op);
  request.gate_ms = gate.gate_ms;
  request.max_ms = gate.max_ms;
  out.curve = ym2612_eg::sample_curve(request);
  out.gate_ms = gate.gate_ms;

  out.content_ms =
      out.curve.points.empty()
          ? 0.0
          : static_cast<double>(out.curve.points.back().ms);
  out.truncated = out.content_ms >= gate.max_ms * 0.999;
  out.span_ms = quantize_span_ms(out.content_ms, previous_span_ms);

  out.attack_end_ms = first_marker_ms(out.curve, MarkerKind::AttackEnd);
  out.decay_end_ms = first_marker_ms(out.curve, MarkerKind::DecayEnd);
  out.key_off_ms = first_marker_ms(out.curve, MarkerKind::KeyOff);

  const int tl_att = static_cast<int>(request.op.tl) * 8;
  out.peak_out = static_cast<uint16_t>(
      std::min(tl_att, static_cast<int>(ym2612_eg::kMaxAttenuation)));
  // The level the decay aims at, in the same output units as the curve.
  out.sustain_out = static_cast<uint16_t>(
      std::min(ym2612_eg::detail::sustain_attenuation(request.op.sl) + tl_att,
               static_cast<int>(ym2612_eg::kMaxAttenuation)));

  // The probe saw the whole loop and the whole hold, so it is the authority on
  // warnings; a short gate can hide a loop from the second pass.
  out.warning = warning_line(probe, request.op);
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
