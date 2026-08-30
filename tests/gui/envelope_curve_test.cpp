#include "../test_check.hpp"
#include "gui/envelope/envelope_curve.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

using namespace ui::envelope;
using ym2612_eg::CurveResult;
using ym2612_eg::MarkerKind;

ym2612::OperatorSettings worked_example() {
  // EG_SPEC's worked example: AR=31 TL=0 DR=10 SL=2 SR=5 RR=7, KS=0.
  ym2612::OperatorSettings op;
  op.attack_rate = 31;
  op.total_level = 0;
  op.decay_rate = 10;
  op.sustain_level = 2;
  op.sustain_rate = 5;
  op.release_rate = 7;
  op.key_scale = 0;
  return op;
}

/// The patch the owner's sweep report was measured on: slow enough that every
/// phase of it outlived the probe that used to size the axis.
ym2612::OperatorSettings owner_report_patch() {
  ym2612::OperatorSettings op;
  op.total_level = 24;
  op.attack_rate = 11;
  op.decay_rate = 3;
  op.sustain_level = 4;
  op.sustain_rate = 2;
  op.release_rate = 4;
  op.key_scale = 0;
  return op;
}

double marker_ms(const CurveResult &curve, MarkerKind kind) {
  for (const ym2612_eg::Marker &m : curve.markers) {
    if (m.kind == kind) {
      return m.ms;
    }
  }
  return -1.0;
}

bool near_rel(double value, double expected, double tolerance) {
  return std::fabs(value - expected) <= std::fabs(expected) * tolerance;
}

// ------------------------------------------------------- params conversion

void test_registers_map_straight_through() {
  ym2612::OperatorSettings op = worked_example();
  op.key_scale = 2;
  const auto params = to_operator_params(op);
  CHECK(params.ar == 31);
  CHECK(params.dr == 10);
  CHECK(params.sr == 5);
  CHECK(params.rr == 7);
  CHECK(params.sl == 2);
  CHECK(params.tl == 0);
  CHECK(params.ks == 2);
  CHECK(params.ssg == 0);
}

void test_ssg_bits_are_packed_the_way_the_chip_wants_them() {
  ym2612::OperatorSettings op;

  // Disabled: the shape bits still go out, exactly as write_settings() sends
  // them, and are inert while bit3 is clear.
  op.ssg_enable = false;
  op.ssg_type_envelope_control = 5;
  CHECK(packed_ssg(op) == 0x05);
  CHECK((packed_ssg(op) & 0x08) == 0);

  // bit3 enable, bit2 attack, bit1 alternate, bit0 hold.
  op.ssg_enable = true;
  op.ssg_type_envelope_control = 0;
  CHECK(packed_ssg(op) == 0x08);
  op.ssg_type_envelope_control = 1; // hold
  CHECK(packed_ssg(op) == 0x09);
  op.ssg_type_envelope_control = 2; // alternate
  CHECK(packed_ssg(op) == 0x0A);
  op.ssg_type_envelope_control = 4; // attack
  CHECK(packed_ssg(op) == 0x0C);
  op.ssg_type_envelope_control = 7; // attack + alternate + hold
  CHECK(packed_ssg(op) == 0x0F);

  for (int type = 0; type < 8; ++type) {
    op.ssg_type_envelope_control = static_cast<uint8_t>(type);
    CHECK(to_operator_params(op).ssg == 0x08 + type);
  }
}

void test_only_envelope_registers_count_as_a_change() {
  const ym2612::OperatorSettings a = worked_example();
  ym2612::OperatorSettings b = a;
  b.multiple = 7;
  b.detune = 3;
  b.amplitude_modulation_enable = true;
  CHECK(same_envelope(to_operator_params(a), to_operator_params(b)));

  b = a;
  b.sustain_rate = 6;
  CHECK(!same_envelope(to_operator_params(a), to_operator_params(b)));
}

// -------------------------------------------------- the held-window policy

/// The tests' own held-forever simulation, and the only horizon left anywhere
/// in this file: megatoy does not run one any more. It is here to be the thing
/// the closed forms are checked AGAINST -- where the decay really ends, where
/// the envelope really parks, how fast the loop really runs -- which is the
/// one job a simulation is better at than a formula, and the one job it was
/// never being asked to do while it was also sizing the axis.
CurveResult simulate_held(const ym2612::OperatorSettings &op,
                          double max_ms = 12000.0) {
  ym2612_eg::CurveRequest request;
  request.op = to_operator_params(op);
  request.pitch = reference_pitch();
  request.gate_ms = -1.0;
  request.max_ms = max_ms;
  return ym2612_eg::sample_curve(request);
}

/// REWRITTEN for the closed-form window. This used to assert the old policy
/// directly -- that the sustain owned between a fifth and seven tenths of the
/// width, and that the whole thing stopped at the 2 s nominal cap. Both were
/// artefacts of sizing the axis from the probe's markers, and neither exists
/// any more: the window is now one smooth function of the envelope's whole
/// lifetime. What the assertions were really *for* survives -- the decay lands
/// inside the window, the sustain gets a readable share of what is left, and
/// the 27 s this envelope takes to die is compressed hard rather than drawn.
void test_a_normal_patch_gets_a_visible_sustain() {
  const auto op = worked_example();
  const auto params = to_operator_params(op);
  const CurveResult held = simulate_held(op);
  const double decay_end = marker_ms(held, MarkerKind::DecayEnd);
  CHECK(decay_end > 0.0);

  // EG_SPEC's worked example takes 27 s to reach silence. The 3 s probe never
  // saw that; the closed form does not have to.
  const double lifetime =
      ym2612_eg::phase_durations(params, reference_pitch()).lifetime_ms();
  CHECK(lifetime > 20000.0);
  CHECK(lifetime < 35000.0);

  const double held_ms = choose_held_ms(params);
  CHECK(held_ms > decay_end);
  const double sustain_share = (held_ms - decay_end) / held_ms;
  CHECK(sustain_share > 0.2);
  // Sub-linear, and hard: a fifth of the life at most.
  CHECK(held_ms < lifetime * 0.2);
  CHECK(held_ms <= 4000.0);
}

/// SR = 0 is a hold that never ends, so the envelope's life really is
/// infinite -- but "holds forever" and "takes a minute and a half" look the
/// same on any axis a screen can hold, so the drawing policy saturates them
/// together rather than sending one to the ceiling and leaving the other in
/// the seconds. What must survive is that the flat part is visible, that the
/// decay before it keeps a readable share, and that a faster SR is narrower.
void test_an_sr0_patch_shows_its_flat_hold() {
  ym2612::OperatorSettings op = worked_example();
  op.sustain_rate = 0; // parks at the sustain level and stays there
  const auto params = to_operator_params(op);

  const CurveResult held = simulate_held(op);
  CHECK(std::isfinite(held.park_ms));
  // The closed form still tells the truth about the envelope ...
  CHECK(!std::isfinite(
      ym2612_eg::phase_durations(params, reference_pitch()).lifetime_ms()));
  const double held_ms = choose_held_ms(params);
  // ... while the axis it is drawn on stays a readable width.
  CHECK(held_ms < 4000.0);
  // The flat part is visible, and the decay before it is not squeezed away.
  CHECK(held_ms > held.park_ms * 1.2);
  // ~19% here: the axis is a little wider than this patch alone needs, which
  // is the price of SR = 0 sitting beside SR = 1 instead of jumping.
  CHECK(held.park_ms / held_ms > 0.15);

  // SR = 1 dies, but so slowly that it must land beside SR = 0, not a cliff
  // away from it.
  op.sustain_rate = 1;
  const double sr1 = choose_held_ms(to_operator_params(op));
  CHECK(near_rel(sr1, held_ms, 0.05));

  // An SR that finishes within the drawable range is narrower.
  op.sustain_rate = 15;
  CHECK(choose_held_ms(to_operator_params(op)) < held_ms);
}

void test_an_ssg_loop_shows_a_few_periods() {
  ym2612::OperatorSettings op;
  op.attack_rate = 31;
  op.decay_rate = 15;
  op.sustain_level = 0;
  op.sustain_rate = 8;
  op.release_rate = 7;
  op.total_level = 0;
  op.ssg_enable = true;
  op.ssg_type_envelope_control = 0; // repeating saw

  const CurveResult held = simulate_held(op);
  CHECK(held.loop_hz > 0.0);
  const double held_ms = choose_held_ms(to_operator_params(op));
  const double period_ms = 1000.0 / held.loop_hz;
  const double periods = held_ms / period_ms;
  CHECK(periods >= 3.0);
  CHECK(periods <= 4.0);
}

/// The owner's report: an SSG patch with AR < 31 spends its whole attack at
/// or above the fold threshold. Until ym2612_eg v0.1.1 made the fold event
/// edge-triggered, every one of those samples counted as a fold, loop_hz came
/// back as the sample rate and the graph collapsed to a tenth of a
/// millisecond wide.
void test_a_slow_attack_ssg_loop_reports_a_musical_rate() {
  ym2612::OperatorSettings op;
  op.attack_rate = 14;
  op.decay_rate = 18;
  op.sustain_level = 9;
  op.sustain_rate = 14;
  op.release_rate = 0;
  op.total_level = 0;
  op.key_scale = 0;
  op.ssg_enable = true;
  op.ssg_type_envelope_control = 4; // inverted saw

  const CurveResult held = simulate_held(op);
  // A loop a listener could hear as a tremolo, not 53 kHz.
  CHECK(held.loop_hz > 1.0);
  CHECK(held.loop_hz < 100.0);
  CHECK(near_rel(held.loop_hz, 6.12, 0.02));

  // ... and the window policy sizes itself from the period rather than from
  // the sample rate.
  const double held_ms = choose_held_ms(to_operator_params(op));
  const double periods = held_ms / (1000.0 / held.loop_hz);
  CHECK(periods >= 3.0);
  CHECK(periods <= 4.0);
}

void test_a_frozen_attack_still_produces_a_usable_window() {
  ym2612::OperatorSettings op = worked_example();
  op.attack_rate = 0; // never sounds; parks immediately

  CHECK(choose_held_ms(to_operator_params(op)) >= kMinHeldMs);
}

void test_the_held_window_is_bounded_across_a_sweep() {
  for (int ar = 0; ar <= 31; ar += 7) {
    for (int dr = 0; dr <= 31; dr += 7) {
      for (int sl = 0; sl <= 15; sl += 5) {
        for (int sr = 0; sr <= 31; sr += 11) {
          ym2612::OperatorSettings op;
          op.attack_rate = static_cast<uint8_t>(ar);
          op.decay_rate = static_cast<uint8_t>(dr);
          op.sustain_level = static_cast<uint8_t>(sl);
          op.sustain_rate = static_cast<uint8_t>(sr);
          op.release_rate = 7;
          const auto params = to_operator_params(op);
          const double held_ms = choose_held_ms(params);
          CHECK(held_ms >= kMinHeldMs);
          CHECK(held_ms <= kMaxHeldMs);
          // The release's budget is its own: nothing about the held envelope
          // may change how far the release is simulated.
          CHECK(release_max_ms() >= 4000.0);
          CHECK(release_max_ms() <= 10000.0);
        }
      }
    }
  }
}

// ----------------------------------------------------- the patches under test

ym2612::OperatorSettings adsr(int ar, int dr, int sl, int sr, int rr, int ks) {
  ym2612::OperatorSettings op;
  op.attack_rate = static_cast<uint8_t>(ar);
  op.decay_rate = static_cast<uint8_t>(dr);
  op.sustain_level = static_cast<uint8_t>(sl);
  op.sustain_rate = static_cast<uint8_t>(sr);
  op.release_rate = static_cast<uint8_t>(rr);
  op.key_scale = static_cast<uint8_t>(ks);
  return op;
}

ym2612::OperatorSettings ssg_patch(int type, int ar, int dr, int sl, int sr,
                                   int rr, int ks) {
  ym2612::OperatorSettings op = adsr(ar, dr, sl, sr, rr, ks);
  op.total_level = 0;
  op.ssg_enable = true;
  op.ssg_type_envelope_control = static_cast<uint8_t>(type & 0x07);
  return op;
}

// -------------------------------------------------------- the width policy

/**
 * A loop whose ramp never finishes is not a slow loop but no loop -- the
 * library says so with an infinite period -- and the axis is then sized by
 * the phase that stalled, exactly as it is for a patch with SSG-EG switched
 * off. The arithmetic that decides which patches those are lives in
 * ym2612_eg; what is tested here is that the width policy believes it.
 */
void test_a_loop_that_never_folds_is_sized_like_a_plain_patch() {
  const auto stalled = ssg_patch(0, 31, 15, 8, 0, 7, 0); // SR = 0
  const auto params = to_operator_params(stalled);
  CHECK(!std::isfinite(
      ym2612_eg::ssg_loop_period_ms(params, reference_pitch())));
  CHECK(choose_held_ms(params) ==
        window_for_timeline_ms(
            ym2612_eg::phase_durations(params, reference_pitch())));
  // ... and a mode that latches rather than folding takes the same road, with
  // a plain zero instead of an infinity.
  const auto latching = to_operator_params(ssg_patch(1, 31, 15, 8, 8, 7, 0));
  CHECK(ym2612_eg::ssg_loop_period_ms(latching, reference_pitch()) == 0.0);
  CHECK(choose_held_ms(latching) ==
        window_for_timeline_ms(
            ym2612_eg::phase_durations(latching, reference_pitch())));
}

/// The warning is two bits and a comparison, not something a run reports.
void test_the_warning_is_read_off_the_registers() {
  CHECK(warning_line(to_operator_params(ssg_patch(0, 30, 15, 4, 8, 7, 0))) !=
        nullptr);
  CHECK(warning_line(to_operator_params(ssg_patch(0, 31, 15, 4, 8, 7, 0))) ==
        nullptr);
  // The same AR without SSG-EG is nobody's business.
  CHECK(warning_line(to_operator_params(adsr(30, 15, 4, 8, 7, 0))) == nullptr);
  // Every mode, including the ones that never fold: the convention is about
  // SSG-EG being on at all, not about whether the shape happens to loop.
  for (int type = 0; type < 8; ++type) {
    CHECK(warning_line(to_operator_params(
              ssg_patch(type, 0, 15, 4, 8, 7, 0))) != nullptr);
    CHECK(warning_line(to_operator_params(
              ssg_patch(type, 31, 15, 4, 8, 7, 0))) == nullptr);
  }
}

/// The compression term. It has to be smooth (no jumps anywhere), monotone (a
/// longer envelope never gets a narrower axis) and sub-linear (a ten-times
/// longer envelope must not get a ten-times wider one, or a slow patch
/// swallows the graph).
void test_the_window_curve_is_smooth_monotone_and_sub_linear() {
  // The three points kWindowRefMs and kWindowExponent were tuned on.
  CHECK(window_for_lifetime_ms(300.0) >= 300.0);
  CHECK(window_for_lifetime_ms(300.0) <= 400.0);
  CHECK(window_for_lifetime_ms(3000.0) >= 1000.0);
  CHECK(window_for_lifetime_ms(3000.0) <= 1500.0);
  CHECK(window_for_lifetime_ms(30000.0) >= 3000.0);
  CHECK(window_for_lifetime_ms(30000.0) <= 5000.0);

  double previous = 0.0;
  for (double lifetime = 1.0; lifetime <= 1.0e6; lifetime *= 1.01) {
    const double window = window_for_lifetime_ms(lifetime);
    CHECK(window >= previous);
    CHECK(window >= kMinHeldMs);
    CHECK(window <= kMaxHeldMs);
    // Smooth: a percent of lifetime can never be more than a percent of axis.
    CHECK(window <= previous * 1.01 || previous == 0.0);
    previous = window;
  }
  // Ten times the life, nowhere near ten times the axis.
  CHECK(window_for_lifetime_ms(30000.0) > window_for_lifetime_ms(3000.0));
  CHECK(window_for_lifetime_ms(30000.0) < window_for_lifetime_ms(3000.0) * 5.0);
  // An envelope that never ends takes the ceiling, and only it does.
  const double forever = std::numeric_limits<double>::infinity();
  CHECK(window_for_lifetime_ms(forever) == kMaxHeldMs);
  CHECK(window_for_lifetime_ms(0.0) == kMinHeldMs);
}

/**
 * The floor under the compression, and why it has to be there.
 *
 * Compressing the lifetime alone will happily hand a patch whose attack takes
 * 8.4 s a 6.4 s axis. The attack then does not finish inside the graph, so the
 * decay and the sustain are not on it at all, and the sliders that shape them
 * look dead. So the axis always reaches past where the sustain begins, and the
 * lifetime only ever widens it further.
 */
void test_the_window_never_cuts_off_the_phase_being_edited() {
  ym2612_eg::PhaseDurations timeline;
  timeline.attack_ms = 8360.0;
  timeline.decay_ms = 460.0;
  timeline.sustain_ms = 200.0;
  // On its own the compression asks for far less than the attack.
  CHECK(window_for_lifetime_ms(timeline.lifetime_ms()) <
        timeline.sustain_start_ms());
  // The window does not.
  const double window = window_for_timeline_ms(timeline);
  CHECK(window > timeline.sustain_start_ms());

  // A quarter of the axis is kept for the sustain, so SR always has somewhere
  // to be -- up to the ceiling, which still wins.
  ym2612_eg::PhaseDurations modest;
  modest.attack_ms = 100.0;
  modest.decay_ms = 200.0;
  modest.sustain_ms = 10.0;
  CHECK(near_rel(window_for_timeline_ms(modest), 400.0, 0.001));
  // The 8.4 s attack still gets an axis that reaches past where the sustain
  // begins, and stays inside the ceiling. Phases are saturated for drawing, so
  // this promise only holds as far as the ceiling does -- an attack longer than
  // that cannot be shown whatever the policy says.
  CHECK(window_for_timeline_ms(timeline) > timeline.sustain_start_ms());
  CHECK(window_for_timeline_ms(timeline) <= kMaxHeldMs);

  // The floor only ever raises: a long-lived envelope with a short attack and
  // decay is still sized by its lifetime.
  ym2612_eg::PhaseDurations long_lived;
  long_lived.attack_ms = 5.0;
  long_lived.decay_ms = 20.0;
  long_lived.sustain_ms = 30000.0;
  CHECK(near_rel(window_for_timeline_ms(long_lived),
                 window_for_lifetime_ms(drawable_lifetime_ms(long_lived)),
                 0.001));
  // 30 s of sustain is past what an axis can hold, so it is drawn as the
  // longest life the policy models rather than as itself.
  CHECK(drawable_lifetime_ms(long_lived) < long_lived.lifetime_ms());
}

/// Whatever the patch, the instant the sustain begins is inside the axis --
/// which is to say every phase the envelope actually has is at least partly
/// drawn. The only exception is an envelope whose attack and decay alone
/// outlast the widest axis there is, and then the ceiling is what stops it.
void test_every_phase_present_is_at_least_partly_visible() {
  for (int ar = 0; ar <= 31; ar += 3) {
    for (int dr = 0; dr <= 31; dr += 5) {
      for (int sl = 0; sl <= 15; sl += 3) {
        for (int ks = 0; ks <= 3; ks += 3) {
          ym2612::OperatorSettings op = adsr(ar, dr, sl, 8, 5, ks);
          const auto params = to_operator_params(op);
          const ym2612_eg::PhaseDurations timeline =
              ym2612_eg::phase_durations(params, reference_pitch());
          const double window = choose_held_ms(params);
          if (timeline.sustain_start_ms() < kMaxHeldMs) {
            CHECK(window > timeline.sustain_start_ms());
          } else {
            CHECK(window == kMaxHeldMs);
          }
        }
      }
    }
  }
}

// ------------------------------------------------------------------ the axis

/**
 * REPLACES the four span-quantiser tests. The axis used to be rounded onto a
 * ladder of a dozen round widths with hysteresis, so that dragging a slider
 * could not make it breathe -- and those tests pinned the ladder's rungs, its
 * two thresholds, its idempotence and how often a full sweep could move it.
 *
 * None of that exists any more. The axis is animated at draw time now
 * (gui/components/envelope_image.cpp), which stops the breathing without also
 * throwing the answer away: the ladder was turning a two percent change of
 * content into a thirty percent change of axis, and back. So the span is simply
 * the content it has to hold, and what is worth asserting is that.
 */
void test_the_span_is_the_content_it_has_to_hold() {
  // The held window is always inside the axis, and so is the release unless the
  // budget has deliberately let it run off the right edge.
  const ym2612::OperatorSettings patches[] = {
      worked_example(),  owner_report_patch(),      adsr(31, 20, 8, 20, 12, 0),
      adsr(6, 4, 10, 3, 2, 0), adsr(31, 8, 2, 6, 10, 0), adsr(20, 15, 6, 10, 9, 3),
  };
  for (const auto &op : patches) {
    const EnvelopeCurve curve = build_envelope_curve(op);
    CHECK(curve.span_ms >= curve.held_ms || curve.span_ms == kMaxHeldMs);
    CHECK(curve.span_ms >= curve.release_content_ms ||
          curve.span_ms >= curve.held_ms * 3.9);
    // No round numbers to land on: the axis is a real width, not a rung.
    CHECK(curve.span_ms >= kMinSpanMs);
  }
}

/// The curve is a pure function of the operator and the reference note now.
/// It used to take the span drawn last frame, because the ladder's hysteresis
/// made the answer depend on where the axis had been.
void test_the_span_does_not_depend_on_where_the_axis_has_been() {
  EnvelopeCurveCache first;
  EnvelopeCurveCache second;
  const ym2612::OperatorSettings a = worked_example();
  const ym2612::OperatorSettings b = adsr(6, 4, 10, 3, 2, 0);

  // One cache walks a -> b -> a, the other only ever sees a.
  first.get(a);
  first.get(b);
  const double after_a_detour = first.get(a).span_ms;
  const double straight = second.get(a).span_ms;
  CHECK(after_a_detour == straight);
  CHECK(build_envelope_curve(a).span_ms == straight);
}

void test_the_grid_step_divides_the_span_sensibly() {
  // Every width the policy can hand it, including the wider axis a slow loop
  // is allowed.
  for (double span = kMinSpanMs; span <= kLoopMaxAxisMs; span += 5.0) {
    const double step = grid_step_ms(span);
    CHECK(step > 0.0);
    // 10000 is the top of the grid ladder, not the axis limit it happens to
    // share a value with: past it the divisions simply get coarser.
    CHECK(span / step <= 6.0 || step == 10000.0);
    CHECK(span / step >= 1.0);
  }
}

// ------------------------------------------------------ end-to-end geometry

bool has_marker(const CurveResult &curve, MarkerKind kind) {
  return marker_ms(curve, kind) >= 0.0;
}

void test_the_worked_examples_decay_lands_on_the_real_millisecond_axis() {
  const EnvelopeCurve curve = build_envelope_curve(worked_example());
  // EG_SPEC: the decay of AR=31 TL=0 DR=10 SL=2 ends on tick 5440 = 306.4 ms.
  CHECK(curve.decay_end_ms > 0.0);
  CHECK(near_rel(curve.decay_end_ms, 306.4, 0.02));

  // Everything the graph draws is on that same axis.
  CHECK(curve.attack_end_ms >= 0.0);
  CHECK(curve.attack_end_ms < curve.decay_end_ms);
  CHECK(curve.held_ms > curve.decay_end_ms);
  CHECK(curve.span_ms >= curve.held_ms);
  // The held trace is simulated across the whole axis, not just the window
  // the policy asked for, so it ends at the right edge rather than in mid-air.
  CHECK(near_rel(curve.held_content_ms, curve.span_ms, 0.01));
  CHECK(!curve.held.points.empty());
  CHECK(curve.warning == nullptr);

  // The key is never released on the held trace, so nothing on it is a
  // key-off and the sustain runs to the end of the window.
  CHECK(!has_marker(curve.held, MarkerKind::KeyOff));
  CHECK(!curve.held_parked); // SR = 5 keeps crawling

  // AR = 31 is an instant attack, so the curve starts at full volume; it is
  // monotonic in time, and it is still sustaining where it stops.
  CHECK(curve.held.points.front().ms == 0.0f);
  CHECK(curve.held.points.front().out == 0);
  CHECK(curve.held.points.back().out < ym2612_eg::kMaxAttenuation);
  CHECK(curve.peak_out == 0); // TL = 0
  CHECK(curve.sustain_out == 64);
  double previous = -1.0;
  bool reached_peak = false;
  for (const auto &p : curve.held.points) {
    CHECK(p.ms >= previous);
    previous = p.ms;
    reached_peak |= (p.out == 0);
  }
  CHECK(reached_peak);
}

/// The second trace: a release from full volume, on the same axis but from
/// t = 0, so it never depends on when a key-off happens.
void test_the_release_starts_at_full_volume_and_reaches_silence() {
  const EnvelopeCurve curve = build_envelope_curve(worked_example());
  CHECK(curve.release.points.size() >= 2);
  CHECK(curve.release.points.front().ms == 0.0f);
  CHECK(curve.release.points.front().out == curve.peak_out);
  CHECK(!curve.release_truncated);
  CHECK(curve.release.points.back().out == ym2612_eg::kMaxAttenuation);
  // RR = 7 at the reference note.
  CHECK(near_rel(curve.release_content_ms, 907.7, 0.02));
  // It falls, and it never rises.
  double previous_out = -1.0;
  for (const auto &p : curve.release.points) {
    CHECK(p.out >= previous_out);
    previous_out = p.out;
  }
}

/// SSG-EG releases through the same rules the chip uses -- the increment is
/// quadrupled and the ramp is cut dead at 0x200 -- so the same RR is several
/// times faster with it than without.
void test_ssg_eg_makes_the_release_dramatically_shorter() {
  ym2612::OperatorSettings plain = worked_example();
  plain.release_rate = 6;
  ym2612::OperatorSettings ssg = plain;
  ssg.ssg_enable = true;
  ssg.ssg_type_envelope_control = 0; // repeating saw

  const EnvelopeCurve plain_curve = build_envelope_curve(plain);
  const EnvelopeCurve ssg_curve = build_envelope_curve(ssg);

  CHECK(near_rel(plain_curve.release_content_ms, 1815.3, 0.02));
  CHECK(near_rel(ssg_curve.release_content_ms, 229.8, 0.02));
  // 4x from the increment, and the rest from the hard cut at 0x200.
  CHECK(ssg_curve.release_content_ms * 4.0 < plain_curve.release_content_ms);
  CHECK(plain_curve.release.points.back().out == ym2612_eg::kMaxAttenuation);
  CHECK(ssg_curve.release.points.back().out == ym2612_eg::kMaxAttenuation);
}

ym2612::OperatorSettings owner_patch(int ar, int dr, int sl, int sr) {
  // The three SSG type 4 patches from the report: TL = 0, KS = 0, RR = 0.
  ym2612::OperatorSettings op;
  op.attack_rate = static_cast<uint8_t>(ar);
  op.decay_rate = static_cast<uint8_t>(dr);
  op.sustain_level = static_cast<uint8_t>(sl);
  op.sustain_rate = static_cast<uint8_t>(sr);
  op.release_rate = 0;
  op.total_level = 0;
  op.key_scale = 0;
  op.ssg_enable = true;
  op.ssg_type_envelope_control = 4;
  return op;
}

/// The held trace is simulated across the whole quantised span, so a loop
/// keeps looping all the way to the right edge instead of being extrapolated
/// along the slope of its last ramp -- the sawtooth that simply stopped.
void test_a_loop_keeps_looping_to_the_right_edge() {
  const ym2612::OperatorSettings patches[] = {
      owner_patch(14, 18, 9, 14),
      owner_patch(29, 1, 0, 7),
      owner_patch(8, 10, 7, 4),
  };
  for (const auto &op : patches) {
    const EnvelopeCurve c = build_envelope_curve(op);
    // A loop a listener could hear, not the sample rate.
    CHECK(c.held.loop_hz > 0.1);
    CHECK(c.held.loop_hz < 100.0);
    // The polyline reaches the edge of the axis on its own.
    CHECK(near_rel(c.held_content_ms, c.span_ms, 0.01));
    CHECK(!c.held_parked);
    // ... and it is still folding when it gets there: the last fold is within
    // one period of the right edge, so no more than one ramp is unfinished.
    const double period_ms = 1000.0 / c.held.loop_hz;
    double last_fold = -1.0;
    int folds = 0;
    for (const auto &m : c.held.markers) {
      if (m.kind == MarkerKind::SsgFold) {
        last_fold = m.ms;
        ++folds;
      }
    }
    CHECK(folds >= 3);
    CHECK(last_fold > c.span_ms - period_ms * 1.05);
    // No key-off inside the graph, whatever gate the run was asked for.
    CHECK(!has_marker(c.held, MarkerKind::KeyOff));
    // The release is a real curve, not the one-sample cut.
    CHECK(c.release_content_ms > 100.0);
  }
}

/// The inverted SSG-EG modes (types 4-7) run the attenuation scale backwards:
/// att = 0 is the QUIETEST point of the ramp and 0x200 is full volume. A
/// release started from 0 is therefore a release from silence, which key-off
/// cuts dead in a single sample -- the ~1 px sliver the owner reported. The
/// release has to start from whatever level output() is loudest at, and then
/// an inverted mode releases at exactly the same speed as its upright twin,
/// because key-off latches the audible level either way.
void test_an_inverted_ssg_release_is_as_long_as_the_upright_one() {
  ym2612::OperatorSettings upright = worked_example();
  upright.release_rate = 6;
  upright.ssg_enable = true;
  upright.ssg_type_envelope_control = 0; // saw, output not inverted
  ym2612::OperatorSettings inverted = upright;
  inverted.ssg_type_envelope_control = 4; // the same saw, inverted

  CHECK(loudest_attenuation(to_operator_params(upright)) == 0);
  CHECK(loudest_attenuation(to_operator_params(inverted)) ==
        ym2612_eg::kSsgFoldAttenuation);

  const EnvelopeCurve up = build_envelope_curve(upright);
  const EnvelopeCurve inv = build_envelope_curve(inverted);

  CHECK(near_rel(up.release_content_ms, 229.8, 0.02));
  CHECK(near_rel(inv.release_content_ms, up.release_content_ms, 0.01));
  // Not the one-sample cut: at 53 kHz that would be 0.02 ms.
  CHECK(inv.release_content_ms > 100.0);
  // Both start from full volume and both end in silence.
  CHECK(inv.release.points.front().out == inv.peak_out);
  CHECK(inv.release.points.back().out == ym2612_eg::kMaxAttenuation);
  CHECK(!inv.release_truncated);
  // ... and it still only ever falls.
  double previous_out = -1.0;
  for (const auto &p : inv.release.points) {
    CHECK(p.out >= previous_out);
    previous_out = p.out;
  }
}

/// SR = 0 is the case a chained graph could not tell the truth about: with a
/// key-off in the picture the hold is cut short, and without one it is simply
/// flat forever.
void test_an_sr0_held_trace_parks_and_stays_flat() {
  ym2612::OperatorSettings op = worked_example();
  op.sustain_rate = 0;
  const EnvelopeCurve curve = build_envelope_curve(op);

  CHECK(curve.held_parked);
  CHECK(!has_marker(curve.held, MarkerKind::KeyOff));
  CHECK(std::isfinite(curve.held.park_ms));
  // It parks the moment the decay reaches the sustain level ...
  CHECK(near_rel(curve.held.park_ms, 306.4, 0.02));
  // ... at the sustain level, and never moves again.
  CHECK(curve.held.points.back().out == curve.sustain_out);
  for (const auto &p : curve.held.points) {
    CHECK(p.out <= curve.sustain_out);
    if (p.ms >= curve.held.park_ms) {
      CHECK(p.out == curve.sustain_out);
    }
  }
  // The window is wider than the park, so the flat stretch is visible rather
  // than a single pixel at the right edge.
  CHECK(curve.held_ms > curve.held.park_ms);
  CHECK(curve.span_ms > curve.held.park_ms);
}

void test_total_level_moves_the_whole_curve_down() {
  ym2612::OperatorSettings op = worked_example();
  op.total_level = 32;
  const EnvelopeCurve curve = build_envelope_curve(op);
  CHECK(curve.peak_out == 32 * 8);
  CHECK(curve.sustain_out == 64 + 32 * 8);
  for (const auto &p : curve.held.points) {
    CHECK(p.out >= curve.peak_out);
  }
  // The release starts from the same peak the held trace does.
  for (const auto &p : curve.release.points) {
    CHECK(p.out >= curve.peak_out);
  }
}

void test_ssg_enabled_curves_fold() {
  ym2612::OperatorSettings op;
  op.attack_rate = 31;
  op.decay_rate = 15;
  op.sustain_level = 0;
  op.sustain_rate = 8;
  op.release_rate = 7;
  op.ssg_enable = true;
  op.ssg_type_envelope_control = 0;

  const EnvelopeCurve curve = build_envelope_curve(op);
  int folds = 0;
  for (const auto &m : curve.held.markers) {
    folds += (m.kind == MarkerKind::SsgFold) ? 1 : 0;
  }
  CHECK(folds >= 3);
  CHECK(!has_marker(curve.held, MarkerKind::KeyOff));
}

// -------------------------------------------------------------- warnings

void test_the_only_warning_is_the_non_standard_ssg_attack() {
  // An SSG-EG mode expects AR = 31; anything less is the one thing the graph
  // says out loud, because the curve alone does not explain it.
  ym2612::OperatorSettings slow_attack;
  slow_attack.attack_rate = 20;
  slow_attack.decay_rate = 15;
  slow_attack.sustain_level = 4;
  slow_attack.sustain_rate = 8;
  slow_attack.release_rate = 7;
  slow_attack.ssg_enable = true;
  slow_attack.ssg_type_envelope_control = 0;
  const EnvelopeCurve slow = build_envelope_curve(slow_attack);
  CHECK(slow.warning != nullptr);
  CHECK(std::string(slow.warning) == "AR<31: non-standard SSG-EG");
  // The same patch without SSG-EG has nothing wrong with it.
  ym2612::OperatorSettings plain = slow_attack;
  plain.ssg_enable = false;
  CHECK(build_envelope_curve(plain).warning == nullptr);

  // Everything else the simulator flags is left to the shape of the curve:
  // a frozen attack is a flat line at silence ...
  ym2612::OperatorSettings frozen = worked_example();
  frozen.attack_rate = 0;
  CHECK(build_envelope_curve(frozen).warning == nullptr);

  // ... a loop with no teeth is plainly not looping ...
  ym2612::OperatorSettings never_loops;
  never_loops.attack_rate = 31;
  never_loops.decay_rate = 15;
  never_loops.sustain_level = 4;
  never_loops.sustain_rate = 0;
  never_loops.release_rate = 7;
  never_loops.ssg_enable = true;
  never_loops.ssg_type_envelope_control = 0;
  const EnvelopeCurve stuck = build_envelope_curve(never_loops);
  CHECK(stuck.warning == nullptr);

  // ... and an audio-rate loop is a solid block of them.
  ym2612::OperatorSettings audio_rate;
  audio_rate.attack_rate = 31;
  audio_rate.decay_rate = 24;
  audio_rate.sustain_level = 15;
  audio_rate.release_rate = 7;
  audio_rate.ssg_enable = true;
  audio_rate.ssg_type_envelope_control = 0;
  const EnvelopeCurve fast = build_envelope_curve(audio_rate);
  CHECK(fast.held.loop_hz > 100.0); // the library still reports it ...
  CHECK(fast.warning == nullptr);   // ... the graph just does not repeat it.

  CHECK(build_envelope_curve(worked_example()).warning == nullptr);
}

// ----------------------------------------------------------------- cache

void test_the_cache_recomputes_only_on_a_real_change() {
  EnvelopeCurveCache cache;
  ym2612::OperatorSettings op = worked_example();

  const double first_span = cache.get(op).span_ms;
  CHECK(cache.rebuild_count() == 1);
  for (int i = 0; i < 10; ++i) {
    CHECK(cache.get(op).span_ms == first_span);
  }
  CHECK(cache.rebuild_count() == 1);

  // Multiple and detune do not shape the envelope.
  op.multiple = 4;
  cache.get(op);
  CHECK(cache.rebuild_count() == 1);

  op.decay_rate = 12;
  cache.get(op);
  CHECK(cache.rebuild_count() == 2);
}

// -------------------------------------------------------- reference note

/// The note is a setting the app pushes in, and reference_pitch() stays the
/// one place it is read from.
// ------------------------------------------------------- the live cursor

/// The release the chip would actually run from `att`, simulated on its own
/// terms -- the thing release_entry_ms() claims is already on the graph.
CurveResult simulated_release_from(const ym2612::OperatorSettings &op,
                                   double att) {
  ym2612_eg::CurveRequest request;
  request.op = to_operator_params(op);
  // Same as the drawn release: AR is irrelevant to a release, and leaving it
  // high would let key_on() snap the start level away.
  request.op.ar = 0;
  request.pitch = reference_pitch();
  request.gate_ms = 0.0;
  request.max_ms = release_max_ms();
  request.start_att = static_cast<uint16_t>(att + 0.5);
  return ym2612_eg::sample_curve(request);
}

/// The first time a trace reaches `att`, by the same interpolation the cursor
/// uses -- so the two sides of the comparison below differ only in which curve
/// they read.
double time_at_att(const CurveResult &curve, double att) {
  const auto &points = curve.points;
  if (points.empty() || att <= points.front().att) {
    return 0.0;
  }
  for (std::size_t i = 1; i < points.size(); ++i) {
    const double a0 = points[i - 1].att;
    const double a1 = points[i].att;
    if (a1 < att) {
      continue;
    }
    if (a1 <= a0) {
      return points[i].ms;
    }
    const double u = (att - a0) / (a1 - a0);
    return points[i - 1].ms + (points[i].ms - points[i - 1].ms) * u;
  }
  return points.back().ms;
}

void test_the_cursor_is_where_the_elapsed_time_says() {
  const EnvelopeCurve curve = build_envelope_curve(worked_example());
  // Held, still moving: the cursor is simply the elapsed time.
  for (const double elapsed : {0.0, 1.0, 12.5, 100.0}) {
    const VoiceCursor cursor =
        cursor_for_voice(curve, elapsed, -1.0, curve.span_ms);
    CHECK(!cursor.released);
    CHECK(std::fabs(cursor.ms - elapsed) < 1e-9);
    CHECK(cursor.silent_for_ms == 0.0);
  }
  // A key that has not been pressed yet cannot put the cursor left of zero.
  CHECK(cursor_for_voice(curve, -5.0, -1.0, curve.span_ms).ms == 0.0);
}

void test_a_cursor_still_moving_leaves_the_graph() {
  ym2612::OperatorSettings op = worked_example();
  // SR > 0: the held envelope keeps crawling, so nothing but the axis stops
  // the cursor -- and the axis does not stop it, it just stops drawing it.
  // A cursor parked on the edge would say the envelope had come to rest there.
  const EnvelopeCurve curve = build_envelope_curve(op);
  CHECK(!curve.held_parked || curve.held.park_ms > curve.span_ms);
  const VoiceCursor cursor =
      cursor_for_voice(curve, curve.span_ms * 4.0, -1.0, curve.span_ms);
  CHECK(cursor.ms > curve.span_ms);
}

void test_a_parked_cursor_stays_where_the_envelope_stopped() {
  ym2612::OperatorSettings op = worked_example();
  op.sustain_rate = 0; // reaches the sustain level and holds
  const EnvelopeCurve curve = build_envelope_curve(op);
  CHECK(curve.held_parked);
  const double park = curve.held.park_ms;
  CHECK(park < curve.span_ms); // the axis is wider, so this is a real clamp

  // Before the park the cursor tracks the elapsed time ...
  CHECK(std::fabs(cursor_for_voice(curve, park * 0.5, -1.0, curve.span_ms).ms -
                  park * 0.5) < 1e-9);
  // ... and after it, it does not move again however long the key is held.
  for (const double elapsed : {park + 1.0, park * 2.0, 60000.0}) {
    const VoiceCursor cursor =
        cursor_for_voice(curve, elapsed, -1.0, curve.span_ms);
    CHECK(std::fabs(cursor.ms - park) < 1e-9);
  }
  // Parked at the sustain level, not at silence: nothing to fade out.
  CHECK(cursor_for_voice(curve, 60000.0, -1.0, curve.span_ms).silent_for_ms ==
        0.0);
}

/**
 * The claim the whole release cursor rests on: a release starting from level L
 * IS the drawn release-from-full-volume, entered later. If that is true, the
 * two traces agree everywhere past the entry point.
 *
 * They do -- up to the one thing no second simulation could fix either. The
 * EG's 12-bit counter is free-running and shared by all 24 operators, and the
 * increment a rate finds depends on where in that counter it lands: the drawn
 * trace arrives at level L with whatever phase climbing from full volume left
 * it, and a release the chip really runs arrives with whatever phase the
 * attack and decay left. So the two can slip by up to one update period of the
 * release rate -- for RR = 7 that is eight EG ticks -- and that slip is a
 * property of the hardware, not of drawing one release instead of six.
 *
 * Where the rate updates every tick and its table row is uniform, there is no
 * phase left to disagree about, and the two agree to within a sample.
 */
void test_the_release_entry_point_matches_a_real_release() {
  const double sample_ms =
      1000.0 / ym2612_eg::sample_rate_hz(ym2612_eg::kNtscClockHz);
  const double eg_tick_ms =
      1000.0 / ym2612_eg::eg_rate_hz(ym2612_eg::kNtscClockHz);

  double worst_periods = 0.0;
  double worst_uniform_samples = 0.0;
  for (int rr = 0; rr <= 15; ++rr) {
    ym2612::OperatorSettings op = worked_example();
    op.release_rate = rr;
    const EnvelopeCurve curve = build_envelope_curve(op);
    const auto params = to_operator_params(op);
    const int rate = ym2612_eg::detail::effective_rate(
        2 * rr + 1, key_scale_value(params, reference_pitch()));
    // How long the chip waits between two chances to act at this rate: the
    // whole of the slip the shared counter can produce.
    const double update_ms =
        static_cast<double>(1 << ym2612_eg::detail::rate_shift(rate)) *
        eg_tick_ms;

    double worst_ms = 0.0;
    // Key-off from partway down the decay, at the sustain level, and from
    // deep into the sustain's own crawl.
    for (const double key_off_ms : {20.0, 40.0, 120.0, 400.0, 1200.0}) {
      // An exact level, so the only thing left to disagree about is the
      // counter phase: an interpolated one would put the two traces a
      // fraction of an attenuation unit apart before either had started.
      const auto att =
          static_cast<uint16_t>(curve_att_at_ms(curve.held, key_off_ms) + 0.5);
      const double entry = release_entry_ms(curve, att);
      const CurveResult real = simulated_release_from(op, att);
      if (real.points.size() < 2 || curve.release.points.size() < 2) {
        continue; // an instant cut has no trajectory to compare
      }
      // Only levels both traces actually reached. RR = 0 never gets to
      // silence at all, so past its budget there is nothing on either side.
      const double ceiling = std::min<double>(curve.release.points.back().att,
                                              real.points.back().att);
      // Compare where each trace reaches a ladder of levels below the one it
      // started at -- a comparison in TIME, which is what a cursor is placed
      // by.
      for (double level = att + 32.0; level <= ceiling; level += 64.0) {
        const double drawn = release_entry_ms(curve, level) - entry;
        const double actual = time_at_att(real, level);
        worst_ms = std::max(worst_ms, std::fabs(drawn - actual));
      }
    }
    // Never worse than the counter phase can account for.
    CHECK(worst_ms <= update_ms);
    worst_periods = std::max(worst_periods, worst_ms / update_ms);

    // A rate that acts every tick, on a row whose eight entries are equal,
    // has no phase to disagree about at all.
    const bool uniform = ym2612_eg::detail::rate_shift(rate) == 0 &&
                         ym2612_eg::detail::kIncTable[rate][0] ==
                             ym2612_eg::detail::kIncTable[rate][1] &&
                         ym2612_eg::detail::kIncTable[rate][1] ==
                             ym2612_eg::detail::kIncTable[rate][3];
    if (uniform) {
      // One EG tick is three output samples, and the envelope cannot move
      // more often than that: agreeing to within a tick is agreeing exactly.
      worst_uniform_samples =
          std::max(worst_uniform_samples, worst_ms / sample_ms);
      CHECK(worst_ms <= eg_tick_ms);
    }
  }
  std::cout << "release entry: worst slip " << worst_periods
            << " of one EG update period; " << worst_uniform_samples
            << " samples where the counter phase cannot matter\n";
  CHECK(worst_periods <= 1.0);
}

/// The key coming up does not move the cursor. The release is drawn from where
/// the note actually let go and the cursor carries straight on into it; only
/// the *shape* comes from the release trace, taken from the point where that
/// trace is already at the level the key came up on.
void test_the_cursor_carries_on_into_the_release() {
  const ym2612::OperatorSettings op = worked_example();
  const EnvelopeCurve curve = build_envelope_curve(op);
  const double key_off_ms = 120.0;
  const double att = curve_att_at_ms(curve.held, key_off_ms);
  const double entry = release_entry_ms(curve, att);
  CHECK(entry > 0.0); // the trace reaches that level partway along, not at 0

  // The instant the key comes up the cursor has not moved ...
  const VoiceCursor at_key_off =
      cursor_for_voice(curve, key_off_ms, 0.0, curve.span_ms);
  CHECK(at_key_off.released);
  CHECK(std::fabs(at_key_off.ms - key_off_ms) < 1e-9);
  // ... the release is drawn from there, with the trace's shape from `entry`.
  CHECK(std::fabs(at_key_off.release_origin_ms - key_off_ms) < 1e-9);
  CHECK(std::fabs(at_key_off.release_from_ms - entry) < 1e-9);

  // From there it advances in real time.
  const VoiceCursor later =
      cursor_for_voice(curve, key_off_ms + 5.0, 5.0, curve.span_ms);
  CHECK(std::fabs(later.ms - (key_off_ms + 5.0)) < 1e-9);

  // Let go at full volume, the shape is the whole trace from its start.
  const VoiceCursor immediate = cursor_for_voice(curve, 0.0, 0.0, curve.span_ms);
  CHECK(immediate.ms <= 1e-9);
  CHECK(immediate.release_from_ms <= 1e-9);
}

void test_a_released_cursor_takes_the_parked_level_with_it() {
  ym2612::OperatorSettings op = worked_example();
  op.sustain_rate = 0;
  const EnvelopeCurve curve = build_envelope_curve(op);
  const double park = curve.held.park_ms;
  // Held far past the park, then released: the level it releases from is the
  // parked one, so both key-offs enter the release at the same point.
  const VoiceCursor soon =
      cursor_for_voice(curve, park + 1.0, 0.0, curve.span_ms);
  const VoiceCursor late = cursor_for_voice(curve, 30000.0, 0.0, curve.span_ms);
  CHECK(std::fabs(soon.release_from_ms - late.release_from_ms) < 1e-9);
  CHECK(std::fabs(soon.ms - late.ms) < 1e-9);
}

void test_a_voice_reports_how_long_it_has_been_silent() {
  const ym2612::OperatorSettings op = worked_example();
  const EnvelopeCurve curve = build_envelope_curve(op);
  CHECK(std::isfinite(curve.release_silence_ms));

  const double att = curve_att_at_ms(curve.held, 0.0);
  const double entry = release_entry_ms(curve, att);
  const double to_silence = curve.release_silence_ms - entry;
  CHECK(to_silence > 0.0);
  // Still falling: audible.
  CHECK(
      cursor_for_voice(curve, to_silence * 0.5, to_silence * 0.5, curve.span_ms)
          .silent_for_ms == 0.0);
  // Past silence, the clock the fade runs on starts.
  const VoiceCursor gone = cursor_for_voice(curve, to_silence + 100.0,
                                            to_silence + 100.0, curve.span_ms);
  CHECK(near_rel(gone.silent_for_ms, 100.0, 0.05));
}

// --------------------------------------------- the voice curve cache's key

void test_two_notes_sharing_a_key_scale_value_share_a_curve() {
  ym2612::OperatorSettings op = worked_example();
  op.key_scale = 0;
  const auto params = to_operator_params(op);

  // C4 and C5 are blocks 4 and 5, and with KS = 0 the ksv is the block's top
  // two bits: the same value. C3 is block 3, which is not.
  const auto c3 = ym2612_eg::NotePitch::from_midi(48);
  const auto c4 = ym2612_eg::NotePitch::from_midi(60);
  const auto c5 = ym2612_eg::NotePitch::from_midi(72);
  CHECK(key_scale_value(params, c4) == key_scale_value(params, c5));
  CHECK(key_scale_value(params, c3) != key_scale_value(params, c4));

  // A reference note whose own ksv is none of theirs, so nothing is answered
  // by the shortcut below.
  set_reference_midi_note(kMinReferenceMidiNote); // C0, block 0
  const EnvelopeCurve reference = build_envelope_curve(op);

  VoiceCurveCache cache;
  int budget = 1;
  const EnvelopeCurve *first = cache.get(op, c4, reference, budget);
  CHECK(first != nullptr);
  CHECK(budget == 0); // one simulation, and it was paid for
  CHECK(cache.rebuild_count() == 1);
  CHECK(cache.size() == 1);
  CHECK(first != &reference);

  // Same ksv: the same entry, no second simulation, and no budget spent --
  // which is what makes a note-on free once the cache is warm.
  const EnvelopeCurve *second = cache.get(op, c5, reference, budget);
  CHECK(second == first);
  CHECK(budget == 0);
  CHECK(cache.rebuild_count() == 1);
  CHECK(cache.size() == 1);

  // A different ksv is a different entry -- and with nothing left in the
  // budget it waits for the next frame rather than simulating now.
  CHECK(cache.get(op, c3, reference, budget) == nullptr);
  CHECK(cache.rebuild_count() == 1);
  budget = 1;
  const EnvelopeCurve *third = cache.get(op, c3, reference, budget);
  CHECK(third != nullptr);
  CHECK(third != first);
  CHECK(cache.rebuild_count() == 2);
  CHECK(cache.size() == 2);

  // A note that shares the REFERENCE note's ksv costs nothing at all: the
  // curve already on screen is its curve.
  budget = 1;
  CHECK(cache.get(op, reference_pitch(), reference, budget) == &reference);
  CHECK(budget == 1);
  CHECK(cache.rebuild_count() == 2);

  set_reference_midi_note(kDefaultReferenceMidiNote);
}

void test_key_scaling_splits_what_it_should() {
  ym2612::OperatorSettings op = worked_example();
  op.key_scale = 3; // ksv is the whole keycode: every block is its own entry
  const auto params = to_operator_params(op);
  const auto c4 = ym2612_eg::NotePitch::from_midi(60);
  const auto c5 = ym2612_eg::NotePitch::from_midi(72);
  CHECK(key_scale_value(params, c4) != key_scale_value(params, c5));

  set_reference_midi_note(kMinReferenceMidiNote);
  const EnvelopeCurve reference = build_envelope_curve(op);
  VoiceCurveCache cache;
  int budget = 2;
  CHECK(cache.get(op, c4, reference, budget) != nullptr);
  CHECK(cache.get(op, c5, reference, budget) != nullptr);
  CHECK(cache.rebuild_count() == 2);
  CHECK(cache.size() == 2);
  set_reference_midi_note(kDefaultReferenceMidiNote);
}

void test_the_voice_cache_never_outgrows_the_six_voices() {
  ym2612::OperatorSettings op = worked_example();
  op.key_scale = 3;
  set_reference_midi_note(kMinReferenceMidiNote);
  const EnvelopeCurve reference = build_envelope_curve(op);
  VoiceCurveCache cache;
  for (int midi = 24; midi <= 96; midi += 3) {
    int budget = 1;
    cache.get(op, ym2612_eg::NotePitch::from_midi(midi), reference, budget);
    CHECK(cache.size() <= VoiceCurveCache::kMaxEntries);
  }
  set_reference_midi_note(kDefaultReferenceMidiNote);
}

void test_a_voice_curve_covers_the_axis_it_is_drawn_on() {
  ym2612::OperatorSettings op = worked_example();
  op.key_scale = 3; // so a high note's envelope is far shorter than C0's
  set_reference_midi_note(kMinReferenceMidiNote);
  const EnvelopeCurve reference = build_envelope_curve(op);
  VoiceCurveCache cache;
  int budget = 1;
  const EnvelopeCurve *voice =
      cache.get(op, ym2612_eg::NotePitch::from_midi(96), reference, budget);
  CHECK(voice != nullptr);
  // Its own window is much narrower, but it was simulated across the axis it
  // will be drawn on, so the overlay never has to be invented past the end.
  CHECK(voice->span_ms < reference.span_ms);
  CHECK(voice->held_content_ms >= reference.span_ms * 0.999 ||
        voice->held_parked);
  set_reference_midi_note(kDefaultReferenceMidiNote);
}

void test_a_register_change_drops_every_voice_curve() {
  ym2612::OperatorSettings op = worked_example();
  set_reference_midi_note(kMinReferenceMidiNote);
  EnvelopeCurve reference = build_envelope_curve(op);
  VoiceCurveCache cache;
  const auto c4 = ym2612_eg::NotePitch::from_midi(60);
  int budget = 1;
  CHECK(cache.get(op, c4, reference, budget) != nullptr);
  CHECK(cache.rebuild_count() == 1);
  CHECK(cache.get(op, c4, reference, budget) != nullptr);
  CHECK(cache.rebuild_count() == 1);

  op.decay_rate = 12;
  reference = build_envelope_curve(op);
  budget = 1;
  CHECK(cache.get(op, c4, reference, budget) != nullptr);
  CHECK(cache.rebuild_count() == 2);
  CHECK(cache.size() == 1);
  set_reference_midi_note(kDefaultReferenceMidiNote);
}

void test_the_reference_note_is_a_setting() {
  const auto middle_c = ym2612_eg::NotePitch::from_midi(60);
  CHECK(reference_midi_note() == kDefaultReferenceMidiNote);
  CHECK(reference_pitch().fnum == middle_c.fnum);
  CHECK(reference_pitch().block == middle_c.block);

  set_reference_midi_note(72); // C5
  CHECK(reference_midi_note() == 72);
  CHECK(reference_pitch().block == 5);
  CHECK(reference_pitch().fnum == ym2612_eg::NotePitch::from_midi(72).fnum);

  // The preference is a plain integer on disk, so it is clamped rather than
  // trusted.
  set_reference_midi_note(-1);
  CHECK(reference_midi_note() == kMinReferenceMidiNote);
  set_reference_midi_note(1000);
  CHECK(reference_midi_note() == kMaxReferenceMidiNote);

  set_reference_midi_note(kDefaultReferenceMidiNote);
}

/// Key scaling is the whole reason the note is worth choosing: the same
/// registers decay far faster high up the keyboard.
void test_key_scaling_follows_the_reference_note() {
  ym2612::OperatorSettings op = worked_example();
  op.key_scale = 3;

  set_reference_midi_note(36); // C2
  const double low = build_envelope_curve(op).decay_end_ms;
  set_reference_midi_note(96); // C7
  const double high = build_envelope_curve(op).decay_end_ms;
  set_reference_midi_note(kDefaultReferenceMidiNote);

  CHECK(low > 0.0);
  CHECK(high > 0.0);
  CHECK(high < low * 0.5);

  // ... while at KS = 0 -- what most patches use -- five octaves move the
  // decay by less than a factor of two. That is the "barely depends on pitch"
  // the single reference note rests on: the rate still picks up keycode >> 3,
  // so it is not nothing, but it is not the eightfold swing above either.
  ym2612::OperatorSettings flat = worked_example();
  flat.key_scale = 0;
  set_reference_midi_note(36);
  const double flat_low = build_envelope_curve(flat).decay_end_ms;
  set_reference_midi_note(96);
  const double flat_high = build_envelope_curve(flat).decay_end_ms;
  set_reference_midi_note(kDefaultReferenceMidiNote);
  CHECK(flat_high < flat_low);
  CHECK(flat_high > flat_low * 0.5);
}

/// The cache header promises this: the note is part of what a cached curve
/// was built at, so moving it invalidates every operator's curve.
void test_the_cache_rebuilds_when_the_reference_note_changes() {
  EnvelopeCurveCache cache;
  ym2612::OperatorSettings op = worked_example();
  op.key_scale = 3;

  cache.get(op);
  cache.get(op);
  CHECK(cache.rebuild_count() == 1);

  set_reference_midi_note(84); // C6
  cache.get(op);
  cache.get(op);
  CHECK(cache.rebuild_count() == 2);

  set_reference_midi_note(kDefaultReferenceMidiNote);
  cache.get(op);
  CHECK(cache.rebuild_count() == 3);
}

/// An audio-rate loop must keep its own scale: the release is far longer than
/// the loop, and letting it set the axis packs the cycles into a solid block.
void test_a_fast_loop_keeps_its_scale() {
  ym2612::OperatorSettings op;
  op.attack_rate = 31;
  op.decay_rate = 24;
  op.sustain_level = 15;
  op.release_rate = 7;
  op.ssg_enable = true;
  op.ssg_type_envelope_control = 0;

  const EnvelopeCurve fast = build_envelope_curve(op);
  CHECK(fast.held.loop_hz > 100.0);
  // The loop, not the release, decides the width.
  CHECK(fast.release_content_ms > fast.span_ms);
  CHECK(fast.span_ms <= fast.held_ms * 3.0);
  // ... and it still gets a readable share of it.
  CHECK(fast.held_ms / fast.span_ms > 0.25);

  // A slow loop already fits, so nothing about it changes.
  ym2612::OperatorSettings slow = op;
  slow.decay_rate = 15;
  const EnvelopeCurve wide = build_envelope_curve(slow);
  CHECK(wide.held.loop_hz < 20.0);
  CHECK(wide.held_ms / wide.span_ms > 0.25);
}

/// The same bargain for a plain patch: RR = 2 takes over ten seconds where the
/// attack and decay take three hundred milliseconds, and an axis wide enough
/// for all of it would leave nothing of the part being edited.
void test_a_very_long_release_does_not_crush_the_held_trace() {
  ym2612::OperatorSettings op = worked_example();
  op.release_rate = 2;
  const EnvelopeCurve slow = build_envelope_curve(op);
  CHECK(slow.release_truncated); // still falling when the budget ran out
  CHECK(slow.release.points.back().out < ym2612_eg::kMaxAttenuation);
  CHECK(slow.held_ms / slow.span_ms > 0.15);

  // A release that fits is drawn whole, on an axis wide enough for it.
  ym2612::OperatorSettings fits = worked_example();
  fits.release_rate = 6;
  const EnvelopeCurve normal = build_envelope_curve(fits);
  CHECK(!normal.release_truncated);
  CHECK(normal.span_ms >= normal.release_content_ms);
}

/// A slow release must be simulated far enough to be worth drawing. Stopping
/// after a few samples would draw a near-flat sliver that reads as "this note
/// never decays".
void test_a_slow_release_is_simulated_to_the_end() {
  ym2612::OperatorSettings op;
  op.attack_rate = 31;
  op.decay_rate = 8;
  op.sustain_rate = 0; // parks in the sustain hold
  op.sustain_level = 2;
  op.release_rate = 4; // seconds long, even at SSG-EG's 4x
  op.ssg_enable = true;
  op.ssg_type_envelope_control = 0;

  const EnvelopeCurve c = build_envelope_curve(op);
  CHECK(!c.release_truncated);
  CHECK(c.release.points.back().out == ym2612_eg::kMaxAttenuation);
  CHECK(c.release_content_ms > 900.0);
  // The held trace parks; the release is what makes the axis wide.
  CHECK(c.held_parked);
  CHECK(c.span_ms >= c.release_content_ms);
}

// -------------------------------------------------- the one-at-a-time sweeps

enum class Field { Ar, Dr, Sl, Sr, Rr };

const char *field_name(Field field) {
  switch (field) {
  case Field::Ar: return "AR";
  case Field::Dr: return "DR";
  case Field::Sl: return "SL";
  case Field::Sr: return "SR";
  case Field::Rr: return "RR";
  }
  return "?";
}

int field_top(Field field) {
  return (field == Field::Sl || field == Field::Rr) ? 15 : 31;
}

/// Every span the axis takes as one register is swept across its whole range,
/// with the rest of the patch held still. Each build starts from a fresh fit
/// (previous span 0) so the quantiser's hysteresis cannot make the answer
/// depend on the order the sweep happened to run in.
std::vector<double> span_sweep(const ym2612::OperatorSettings &base,
                               Field field) {
  std::vector<double> spans;
  for (int value = 0; value <= field_top(field); ++value) {
    ym2612::OperatorSettings op = base;
    switch (field) {
    case Field::Ar: op.attack_rate = static_cast<uint8_t>(value); break;
    case Field::Dr: op.decay_rate = static_cast<uint8_t>(value); break;
    case Field::Sl: op.sustain_level = static_cast<uint8_t>(value); break;
    case Field::Sr: op.sustain_rate = static_cast<uint8_t>(value); break;
    case Field::Rr: op.release_rate = static_cast<uint8_t>(value); break;
    }
    spans.push_back(build_envelope_curve(op).span_ms);
  }
  return spans;
}

/// Two spans that are the same width.
///
/// As SL moves attenuation from one phase to the next, the same total lifetime
/// is summed in a different order, so an axis that is mathematically unchanged
/// can differ in the last bits of a double. A pixel is a few tenths of a
/// percent of the axis; this is fifty million times finer.
bool same_span(double a, double b) {
  return std::fabs(a - b) <= std::fabs(b) * 1e-9;
}

bool moves(const std::vector<double> &spans) {
  for (const double span : spans) {
    if (!same_span(span, spans.front())) {
      return true;
    }
  }
  return false;
}

/// The base patches the sweeps run over: the owner's report, EG_SPEC's worked
/// example, one fast patch, one keyed hard by KS, and two slow enough that
/// every phase outlives the old 3 s probe.
const ym2612::OperatorSettings &sweep_base(int i) {
  static const ym2612::OperatorSettings bases[] = {
      owner_report_patch(),      worked_example(),
      adsr(31, 20, 8, 20, 12, 0), adsr(20, 15, 6, 10, 9, 3),
      adsr(6, 4, 10, 3, 2, 0),   adsr(31, 8, 2, 6, 10, 0),
  };
  return bases[i];
}
constexpr int kSweepBases = 6;

/**
 * The bug this test exists for, in the owner's own measurements: sweeping one
 * register at a time left SL and SR with no effect on the axis *at all*, and
 * made DR jump threefold in the middle of its range. Every one of those faults
 * was the same fault -- the window policy read the probe's markers, and the
 * probe stopped at 3 s, so an envelope slower than the horizon reported no
 * decay and no park and the policy concluded there was nothing to show.
 *
 * So: sweep each register alone across its whole range, over base patches slow
 * enough that the old horizon would have swallowed them, and require the axis
 * to be monotone in every one of them.
 *
 * The rates are one-directional: a faster rate never widens the axis, a slower
 * one never narrows it. SL is monotone as well but its *direction* is a
 * property of the patch rather than of SL, and deliberately so -- see the test
 * below.
 */
void test_every_rate_moves_the_axis_monotonically() {
  for (const Field field : {Field::Ar, Field::Dr, Field::Sr, Field::Rr}) {
    bool moved_somewhere = false;
    for (int i = 0; i < kSweepBases; ++i) {
      const std::vector<double> spans = span_sweep(sweep_base(i), field);
      for (size_t v = 1; v < spans.size(); ++v) {
        if (spans[v] > spans[v - 1] && !same_span(spans[v], spans[v - 1])) {
          std::cout << field_name(field) << " widened the axis at " << v
                    << " on base " << i << ": " << spans[v - 1] << " -> "
                    << spans[v] << "\n";
        }
        CHECK(spans[v] <= spans[v - 1] || same_span(spans[v], spans[v - 1]));
        // ... and it is continuous while it narrows: no neighbouring pair of
        // registers may move the axis more than one rung of the ladder. Value
        // 0 is exempt because "never advances at all" is a discontinuity of
        // the hardware, not of the policy.
        if (v >= 2) {
          CHECK(spans[v] >= spans[v - 1] * 0.49);
        }
      }
      moved_somewhere |= moves(spans);
    }
    // "No effect at all" is the bug being fixed.
    CHECK(moved_somewhere);
  }
}

/**
 * SL, which the owner measured as having no effect whatever.
 *
 * It has one now. It is not monotone, and it cannot be, because the two things
 * the axis is made of pull opposite ways as SL rises. Raising SL hands
 * attenuation from the sustain phase to the decay phase: the sustain's *start*
 * therefore always moves later, so the floor under the axis always rises; but
 * the envelope's whole *life* only lengthens when DR is the slower of the two
 * rates, and in the ordinary case -- a quick decay into a long slow fade -- it
 * shortens, and dramatically. AR31 DR15 SL0 SR8 takes 9.7 s to fade from full
 * volume; the same patch at SL15 is finished in 1.0 s, because DR does nearly
 * all of the falling. A wider axis for the second would be a lie.
 *
 * The axis is the larger of those two, so it is quasi-convex: it may fall and
 * it may rise, but it never rises and then falls. That is the property worth
 * asserting -- it is what rules out a jitter, which is a fall and a rise inside
 * a couple of neighbouring values.
 */
void test_sustain_level_moves_the_axis_without_ever_doubling_back() {
  bool moved_somewhere = false;
  for (int i = 0; i < kSweepBases; ++i) {
    const std::vector<double> spans = span_sweep(sweep_base(i), Field::Sl);
    bool risen = false;
    for (size_t v = 1; v < spans.size(); ++v) {
      const bool up = spans[v] > spans[v - 1] && !same_span(spans[v], spans[v - 1]);
      const bool down = spans[v] < spans[v - 1] && !same_span(spans[v], spans[v - 1]);
      risen |= up;
      // Once the sustain's start is what sizes the axis, nothing takes it back.
      CHECK(!(risen && down));
    }
    moved_somewhere |= moves(spans);
  }
  CHECK(moved_somewhere);

  // Both directions, named. A slow decay into a fast fade lengthens with SL ...
  const std::vector<double> slow_decay = span_sweep(adsr(31, 4, 0, 20, 12, 0), Field::Sl);
  CHECK(slow_decay.back() > slow_decay.front());
  // ... and a quick decay into a slow fade shortens with it, as long as the
  // decay stays short enough that the floor never takes over.
  const std::vector<double> quick_decay = span_sweep(adsr(31, 20, 0, 4, 12, 0), Field::Sl);
  CHECK(quick_decay.back() < quick_decay.front());
}

/**
 * The owner's second report: TL0 AR1 DR12 SL6 RR5 at the reference note.
 *
 * AR = 1 makes the attack alone last 8.4 s. Sizing the axis from the compressed
 * lifetime and nothing else gave 10 s at SR = 0 and then 6.4 s the moment SR
 * went to 1 -- at which point the attack no longer finished inside the graph,
 * and the sustain, the very thing SR governs, was entirely off the right edge.
 * Raising the sustain rate made the sustain impossible to see.
 */
void test_a_very_slow_attack_still_leaves_room_for_the_sustain() {
  double previous = 0.0;
  for (int sr = 0; sr <= 31; ++sr) {
    ym2612::OperatorSettings op = adsr(1, 12, 6, sr, 5, 0);
    op.total_level = 0;
    const auto params = to_operator_params(op);
    const ym2612_eg::PhaseDurations timeline =
        ym2612_eg::phase_durations(params, reference_pitch());
    const EnvelopeCurve curve = build_envelope_curve(op);

    // The attack really is that long, and the sustain really does start after
    // the probe that used to size the axis would have given up.
    CHECK(near_rel(timeline.attack_ms, 8360.0, 0.02));
    CHECK(timeline.sustain_start_ms() > 3000.0);
    // ... and the axis reaches past it, for every SR.
    CHECK(curve.span_ms > timeline.sustain_start_ms());
    // The trace agrees: the attack ends inside the graph, and so does the decay
    // (SR = 0 parks the instant the decay lands, before the marker fires).
    CHECK(curve.attack_end_ms >= 0.0);
    CHECK(curve.attack_end_ms < curve.span_ms);
    if (sr > 0) {
      CHECK(curve.decay_end_ms >= 0.0);
      CHECK(curve.decay_end_ms < curve.span_ms);
    }
    if (previous > 0.0) {
      CHECK(curve.span_ms <= previous);
    }
    previous = curve.span_ms;
  }
}

} // namespace

/// The axis must be a continuous function of the parameters. It used to switch
/// policy at the probe's horizon: an envelope reaching silence a percent past
/// 3 s counted as "still moving" and the axis jumped sixfold between two
/// neighbouring sustain levels.
///
/// REWRITTEN, and wired into main() -- it was never called before, so the
/// guarantee it names was never actually checked, and its bound was in fact
/// wrong: SL = 15 is a discontinuity of the *chip*, not of the policy.
/// sustain_attenuation() steps by 32 for every SL up to 14 and then jumps
/// straight to 0x3E0, so SL 14 -> 15 hands the whole rest of the scale to the
/// decay in one move. The axis follows, as it should; the smoothness claim is
/// about the fifteen steps either side of it.
void test_the_axis_does_not_jump_between_neighbouring_values() {
  const auto sweep = [](int sl, int sr) {
    ym2612::OperatorSettings op;
    op.attack_rate = 4;
    op.decay_rate = 26;
    op.sustain_level = static_cast<uint8_t>(sl);
    op.sustain_rate = static_cast<uint8_t>(sr);
    op.release_rate = 7;
    op.key_scale = 3; // the envelope lengths that straddled the old horizon
    return build_envelope_curve(op).span_ms;
  };

  for (int sr = 1; sr <= 6; ++sr) {
    double previous = sweep(0, sr);
    for (int sl = 1; sl <= 14; ++sl) {
      const double span = sweep(sl, sr);
      // One rung of the ladder either way; never a policy switch.
      CHECK(span <= previous * 2.01);
      CHECK(span >= previous * 0.49);
      previous = span;
    }
    // ... and SL = 15 moves it, because the chip's own table does.
    CHECK(sweep(15, sr) < previous);
  }
  // The matching guarantee along the rate axes is asserted inside
  // test_every_rate_moves_the_axis_monotonically(), which already has the
  // sweeps in hand.
}

/**
 * A slow loop must not lose its periods, whatever it is that would take them.
 *
 * EXTENDED to DR = 1, which is where the last of the horizons was. The window
 * has always been 3.5 periods wide; what kept moving was what could stop it
 * being that. First the 5 s ceiling, so a loop slower than about 1.4 s per
 * period drew fewer and fewer cycles as DR fell. Then, once the ceiling gave
 * way, the measurement itself: the period came from counting the folds a 12 s
 * probe saw, and one ramp of this patch takes 15 s at DR = 1. The probe saw
 * none, reported no loop, and the graph fell back to the policy for a patch
 * that does not loop -- a 10 s axis with not one cycle on it. DR = 2 put a
 * single fold inside the window and the axis jumped to 23.6 s.
 *
 * So the sweep now runs the whole way down, and the two things it asks for are
 * the two the horizon broke: a slower loop never gets a narrower axis, and one
 * whole period stays on it at every single DR.
 */
void test_a_slow_loop_still_shows_a_period() {
  const auto patch = [](int dr) {
    ym2612::OperatorSettings op;
    op.attack_rate = 31;
    op.decay_rate = static_cast<uint8_t>(dr);
    op.sustain_level = 15;
    op.sustain_rate = 0;
    op.release_rate = 2;
    op.ssg_enable = true;
    op.ssg_type_envelope_control = 2; // triangle: two ramps per period
    return op;
  };

  double previous = 0.0;
  for (int dr = 20; dr >= 1; --dr) {
    const ym2612::OperatorSettings op = patch(dr);
    const double period =
        ym2612_eg::ssg_loop_period_ms(to_operator_params(op),
                                      reference_pitch());
    const EnvelopeCurve c = build_envelope_curve(op);
    size_t folds = 0;
    for (const ym2612_eg::Marker &m : c.held.markers) {
      if (m.kind == MarkerKind::SsgFold && m.ms <= c.span_ms) {
        ++folds;
      }
    }
    std::cout << "DR " << dr << ": period " << period << " ms, span "
              << c.span_ms << " ms, " << folds << " folds\n";
    // Two folds is one whole period of an alternating mode. A loop slower than
    // the ceiling cannot have one -- there is no axis long enough -- so what is
    // promised there is the widest axis there is, and the ramp on it.
    if (period * kLoopVisiblePeriods <= kLoopMaxAxisMs) {
      CHECK(folds >= 2);
    } else {
      CHECK(near_rel(c.span_ms, kLoopMaxAxisMs, 0.001));
      CHECK(folds >= 1);
    }
    // ... and the axis is wide enough to hold one, which is the promise the
    // fold count above is only the evidence for.
    // The axis holds a whole period, unless no axis could.
    CHECK(c.span_ms >= std::min(period, kLoopMaxAxisMs));
    // A slower loop never gets a narrower axis.
    CHECK(c.span_ms >= previous * 0.999);
    previous = c.span_ms;
  }
}

/// A held SSG loop never parks, so its cursor must go round with the sound
/// rather than stopping at the right-hand edge while the note carries on
/// looping. It wraps over the whole periods the axis draws.
void test_a_held_loop_cursor_goes_round() {
  ym2612::OperatorSettings op;
  op.attack_rate = 31;
  op.decay_rate = 15;
  op.sustain_level = 15;
  op.sustain_rate = 0;
  op.release_rate = 7;
  op.ssg_enable = true;
  op.ssg_type_envelope_control = 0; // repeating saw
  const EnvelopeCurve c = build_envelope_curve(op);
  CHECK(c.held.loop_hz > 0.0);
  const double period = 1000.0 / c.held.loop_hz;

  double first_fold = -1.0;
  double last_fold = -1.0;
  for (const ym2612_eg::Marker &m : c.held.markers) {
    if (m.kind != MarkerKind::SsgFold || m.ms > c.span_ms) {
      continue;
    }
    if (first_fold < 0.0) {
      first_fold = m.ms;
    }
    last_fold = m.ms;
  }
  CHECK(first_fold > 0.0);
  CHECK(last_fold > first_fold);

  // Inside the drawn cycles the cursor simply tracks elapsed time.
  const VoiceCursor early = cursor_for_voice(c, first_fold * 0.5, -1.0, c.span_ms);
  CHECK(near_rel(early.ms, first_fold * 0.5, 0.001));

  // Past them it comes back round instead of sticking at the edge, and it
  // comes back to the same place one period later.
  const double beyond = last_fold + period * 2.5;
  const VoiceCursor wrapped = cursor_for_voice(c, beyond, -1.0, c.span_ms);
  CHECK(wrapped.ms < last_fold);
  CHECK(wrapped.ms >= first_fold);
  const VoiceCursor next =
      cursor_for_voice(c, beyond + (last_fold - first_fold), -1.0, c.span_ms);
  CHECK(near_rel(next.ms, wrapped.ms, 0.001));

  // A patch that does not loop does not come back round: its cursor runs off
  // the end of the axis and is simply not drawn there.
  ym2612::OperatorSettings plain = op;
  plain.ssg_enable = false;
  const EnvelopeCurve pc = build_envelope_curve(plain);
  const VoiceCursor gone =
      cursor_for_voice(pc, pc.span_ms * 5.0, -1.0, pc.span_ms);
  CHECK(gone.ms > pc.span_ms || pc.held_parked);
}

/// A released voice needs to know where on the release trace its own release
/// begins -- the graph draws that stretch as a line, since the drawn release
/// area is the one a note let go at full volume would take, not this one.
void test_a_released_voice_reports_where_its_release_begins() {
  ym2612::OperatorSettings op = worked_example();
  const EnvelopeCurve c = build_envelope_curve(op);

  // Held: no release to draw yet.
  const VoiceCursor held = cursor_for_voice(c, 100.0, -1.0, c.span_ms);
  CHECK(held.release_from_ms < 0.0);
  CHECK(!held.released);

  // Let go while the decay is still running: the release starts high on the
  // trace, near its beginning.
  const double early_off = c.attack_end_ms + 1.0;
  const VoiceCursor early = cursor_for_voice(c, early_off + 5.0, 5.0, c.span_ms);
  CHECK(early.released);
  CHECK(early.release_from_ms >= 0.0);

  // Let go from the sustain, lower down: further along the trace, because the
  // drawn release takes time to fall that far.
  const double late_off = c.decay_end_ms + 50.0;
  const VoiceCursor late = cursor_for_voice(c, late_off + 5.0, 5.0, c.span_ms);
  CHECK(late.release_from_ms > early.release_from_ms);

  // And the cursor is where the key came up plus however long it has been
  // falling -- the entry point only sets the shape, not the position.
  CHECK(near_rel(late.ms, late.release_origin_ms + 5.0, 0.001));
}

/// A voice draws the road it has travelled, not the road ahead: the ghost
/// stops where the note has actually got to, and stops growing at key-off. A
/// loop that has already come round once has been through the whole axis.
void test_a_voice_draws_only_what_it_has_been_through() {
  const ym2612::OperatorSettings op = worked_example();
  const EnvelopeCurve c = build_envelope_curve(op);

  // Partway in, the ghost reaches exactly as far as the note has.
  const VoiceCursor early = cursor_for_voice(c, 40.0, -1.0, c.span_ms);
  CHECK(near_rel(early.held_to_ms, 40.0, 0.001));

  // It never runs past the axis.
  const VoiceCursor beyond =
      cursor_for_voice(c, c.span_ms * 3.0, -1.0, c.span_ms);
  CHECK(near_rel(beyond.held_to_ms, c.span_ms, 0.001));

  // Once the key is up it stops growing: the held part is frozen at the
  // instant of key-off however long the release runs on.
  const double key_off = 40.0;
  const VoiceCursor just_off = cursor_for_voice(c, key_off, 0.0, c.span_ms);
  const VoiceCursor long_off =
      cursor_for_voice(c, key_off + 500.0, 500.0, c.span_ms);
  CHECK(near_rel(just_off.held_to_ms, key_off, 0.001));
  CHECK(near_rel(long_off.held_to_ms, key_off, 0.001));

  // A loop past its first pass has been through all of it.
  ym2612::OperatorSettings loop = op;
  loop.ssg_enable = true;
  loop.ssg_type_envelope_control = 0;
  loop.sustain_level = 15;
  loop.decay_rate = 15;
  const EnvelopeCurve lc = build_envelope_curve(loop);
  CHECK(lc.held.loop_hz > 0.0);
  const VoiceCursor round =
      cursor_for_voice(lc, lc.span_ms * 2.5, -1.0, lc.span_ms);
  CHECK(near_rel(round.held_to_ms, lc.span_ms, 0.001));
  // ... and its cursor has still come back round rather than run off.
  CHECK(round.ms <= lc.span_ms);
}

int main() {
  test_registers_map_straight_through();
  test_ssg_bits_are_packed_the_way_the_chip_wants_them();
  test_only_envelope_registers_count_as_a_change();

  test_a_normal_patch_gets_a_visible_sustain();
  test_an_sr0_patch_shows_its_flat_hold();
  test_an_ssg_loop_shows_a_few_periods();
  test_a_slow_attack_ssg_loop_reports_a_musical_rate();
  test_a_frozen_attack_still_produces_a_usable_window();
  test_the_held_window_is_bounded_across_a_sweep();

  // The closed forms themselves -- the phase durations and the loop period --
  // live in ym2612_eg now, and so do the sweeps that cross-check them against
  // the simulator. What is left here is the policy built on top of them.
  test_a_loop_that_never_folds_is_sized_like_a_plain_patch();
  test_the_warning_is_read_off_the_registers();
  test_the_window_curve_is_smooth_monotone_and_sub_linear();
  test_the_window_never_cuts_off_the_phase_being_edited();
  test_every_phase_present_is_at_least_partly_visible();
  test_every_rate_moves_the_axis_monotonically();
  test_sustain_level_moves_the_axis_without_ever_doubling_back();
  test_a_very_slow_attack_still_leaves_room_for_the_sustain();
  // Never called until now, so the axis-continuity guard it names was not
  // actually guarding anything.
  test_the_axis_does_not_jump_between_neighbouring_values();

  test_the_span_is_the_content_it_has_to_hold();
  test_the_span_does_not_depend_on_where_the_axis_has_been();
  test_the_grid_step_divides_the_span_sensibly();

  test_the_worked_examples_decay_lands_on_the_real_millisecond_axis();
  test_the_release_starts_at_full_volume_and_reaches_silence();
  test_ssg_eg_makes_the_release_dramatically_shorter();
  test_a_loop_keeps_looping_to_the_right_edge();
  test_an_inverted_ssg_release_is_as_long_as_the_upright_one();
  test_an_sr0_held_trace_parks_and_stays_flat();
  test_total_level_moves_the_whole_curve_down();
  test_ssg_enabled_curves_fold();

  test_a_fast_loop_keeps_its_scale();
  // Never called until now either, so the ceiling it guards was unguarded --
  // and the horizon that replaced the ceiling went unnoticed for the same
  // reason. It sweeps DR to 1 now, which is where that horizon was.
  test_a_slow_loop_still_shows_a_period();
  test_a_very_long_release_does_not_crush_the_held_trace();
  test_a_slow_release_is_simulated_to_the_end();

  test_the_only_warning_is_the_non_standard_ssg_attack();
  test_the_cache_recomputes_only_on_a_real_change();

  test_the_cursor_is_where_the_elapsed_time_says();
  test_a_cursor_still_moving_leaves_the_graph();
  test_a_parked_cursor_stays_where_the_envelope_stopped();
  test_the_release_entry_point_matches_a_real_release();
  test_the_cursor_carries_on_into_the_release();
  test_a_released_cursor_takes_the_parked_level_with_it();
  test_a_voice_reports_how_long_it_has_been_silent();
  test_a_held_loop_cursor_goes_round();
  test_a_voice_draws_only_what_it_has_been_through();
  test_a_released_voice_reports_where_its_release_begins();

  test_two_notes_sharing_a_key_scale_value_share_a_curve();
  test_key_scaling_splits_what_it_should();
  test_the_voice_cache_never_outgrows_the_six_voices();
  test_a_voice_curve_covers_the_axis_it_is_drawn_on();
  test_a_register_change_drops_every_voice_curve();

  test_the_reference_note_is_a_setting();
  test_key_scaling_follows_the_reference_note();
  test_the_cache_rebuilds_when_the_reference_note_changes();

  std::cout << "envelope_curve_test passed\n";
  return 0;
}
