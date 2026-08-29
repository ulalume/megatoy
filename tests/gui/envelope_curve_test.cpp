#include "../test_check.hpp"
#include "gui/envelope/envelope_curve.hpp"

#include <cmath>
#include <iostream>
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

CurveResult probe(const ym2612::OperatorSettings &op, double max_ms = 0.0) {
  ym2612_eg::CurveRequest request;
  request.op = to_operator_params(op);
  request.pitch = reference_pitch();
  request.gate_ms = -1.0;
  request.max_ms = max_ms > 0.0 ? max_ms : probe_max_ms(request.op);
  return ym2612_eg::sample_curve(request);
}

void test_a_normal_patch_gets_a_visible_sustain() {
  const auto op = worked_example();
  const CurveResult held = probe(op);
  const double decay_end = marker_ms(held, MarkerKind::DecayEnd);
  CHECK(decay_end > 0.0);

  const double held_ms = choose_held_ms(held, to_operator_params(op));
  CHECK(held_ms > decay_end);
  // A quarter of the held window is sustain, so SR always has something to
  // highlight.
  const double sustain_share = (held_ms - decay_end) / held_ms;
  CHECK(near_rel(sustain_share, 0.25, 0.02));
  // The 27 s sustain decay of this patch must not set the width.
  CHECK(held_ms <= 2000.0);
}

void test_an_sr0_patch_shows_its_flat_hold() {
  ym2612::OperatorSettings op = worked_example();
  op.sustain_rate = 0; // parks at the sustain level and stays there

  const CurveResult held = probe(op);
  CHECK(std::isfinite(held.park_ms));
  const double held_ms = choose_held_ms(held, to_operator_params(op));
  // The flat part has to be visible, not a single pixel at the right edge.
  CHECK(held_ms > held.park_ms * 1.2);
  CHECK(held_ms <= 2000.0);
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

  const CurveResult held = probe(op);
  CHECK(held.loop_hz > 0.0);
  const double held_ms = choose_held_ms(held, to_operator_params(op));
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

  const CurveResult held = probe(op);
  // A loop a listener could hear as a tremolo, not 53 kHz.
  CHECK(held.loop_hz > 1.0);
  CHECK(held.loop_hz < 100.0);
  CHECK(near_rel(held.loop_hz, 6.12, 0.02));

  // ... and the window policy sizes itself from the period rather than from
  // the sample rate.
  const double held_ms = choose_held_ms(held, to_operator_params(op));
  const double periods = held_ms / (1000.0 / held.loop_hz);
  CHECK(periods >= 3.0);
  CHECK(periods <= 4.0);
}

void test_a_frozen_attack_still_produces_a_usable_window() {
  ym2612::OperatorSettings op = worked_example();
  op.attack_rate = 0; // never sounds; parks immediately

  const CurveResult held = probe(op);
  CHECK(choose_held_ms(held, to_operator_params(op)) >= 50.0);
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
          const double held_ms = choose_held_ms(probe(op), params);
          CHECK(held_ms >= 50.0);
          CHECK(held_ms <= 10000.0);
          // The release is simulated for as long as it could be drawn, never
          // for longer than the 10 s ceiling.
          const double budget = release_max_ms(held_ms);
          CHECK(budget >= 4000.0);
          CHECK(budget <= 10000.0);
        }
      }
    }
  }
}

// ------------------------------------------------------- the span quantiser

void test_a_fresh_span_fits_the_content() {
  CHECK(quantize_span_ms(0.0, 0.0) == 25.0);
  CHECK(quantize_span_ms(20.0, 0.0) == 25.0);
  CHECK(quantize_span_ms(30.0, 0.0) == 50.0);
  CHECK(quantize_span_ms(300.0, 0.0) == 500.0);
  // Beyond the ladder the axis stops growing rather than inventing a width.
  CHECK(quantize_span_ms(1.0e9, 0.0) == 10000.0);
}

void test_the_span_holds_still_inside_the_hysteresis_band() {
  const double span = quantize_span_ms(300.0, 0.0); // 500
  // Content wandering between 40% and 95% of the span leaves it alone.
  for (double content = 0.41 * span; content < 0.94 * span; content += 1.0) {
    CHECK(quantize_span_ms(content, span) == span);
  }
}

void test_the_span_never_oscillates_across_a_sweep() {
  // Every content length, at every span it could plausibly be drawn at:
  // one application must be a fixed point of the next.
  for (double content = 0.0; content <= 12000.0; content += 3.7) {
    double span = 0.0;
    for (int i = 0; i < 8; ++i) {
      const double next = quantize_span_ms(content, span);
      if (i > 0) {
        CHECK(next == span); // settled after the first application
      }
      span = next;
      CHECK(span > 0.0);
      CHECK(content <= span || span == 10000.0);
    }
  }
}

void test_a_sweep_up_and_back_down_does_not_flap() {
  // Walk the content up through the whole range and back, and count how often
  // the axis changes. A jittering axis would change on nearly every step.
  double span = 0.0;
  int changes = 0;
  const int steps = 2000;
  for (int i = 0; i <= steps; ++i) {
    const double content = 5000.0 * static_cast<double>(i) / steps;
    const double next = quantize_span_ms(content, span);
    changes += (next != span) ? 1 : 0;
    span = next;
  }
  for (int i = steps; i >= 0; --i) {
    const double content = 5000.0 * static_cast<double>(i) / steps;
    const double next = quantize_span_ms(content, span);
    changes += (next != span) ? 1 : 0;
    span = next;
  }
  // 14 ladder values, walked up once and down once, plus the first fit.
  CHECK(changes <= 28);
}

void test_the_grid_step_divides_the_span_sensibly() {
  for (double span = 25.0; span <= 10000.0; span += 5.0) {
    const double step = grid_step_ms(span);
    CHECK(step > 0.0);
    CHECK(span / step <= 6.0 || step == 10000.0);
    CHECK(span / step >= 1.0);
  }
}

// ------------------------------------------------------ end-to-end geometry

bool has_marker(const CurveResult &curve, MarkerKind kind) {
  return marker_ms(curve, kind) >= 0.0;
}

void test_the_worked_examples_decay_lands_on_the_real_millisecond_axis() {
  const EnvelopeCurve curve = build_envelope_curve(worked_example(), 0.0);
  // EG_SPEC: the decay of AR=31 TL=0 DR=10 SL=2 ends on tick 5440 = 306.4 ms.
  CHECK(curve.decay_end_ms > 0.0);
  CHECK(near_rel(curve.decay_end_ms, 306.4, 0.02));

  // Everything the graph draws is on that same axis.
  CHECK(curve.attack_end_ms >= 0.0);
  CHECK(curve.attack_end_ms < curve.decay_end_ms);
  CHECK(curve.held_ms > curve.decay_end_ms);
  CHECK(near_rel(curve.held_content_ms, curve.held_ms, 0.01));
  CHECK(curve.span_ms >= curve.held_ms);
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
  const EnvelopeCurve curve = build_envelope_curve(worked_example(), 0.0);
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

  const EnvelopeCurve plain_curve = build_envelope_curve(plain, 0.0);
  const EnvelopeCurve ssg_curve = build_envelope_curve(ssg, 0.0);

  CHECK(near_rel(plain_curve.release_content_ms, 1815.3, 0.02));
  CHECK(near_rel(ssg_curve.release_content_ms, 229.8, 0.02));
  // 4x from the increment, and the rest from the hard cut at 0x200.
  CHECK(ssg_curve.release_content_ms * 4.0 < plain_curve.release_content_ms);
  CHECK(plain_curve.release.points.back().out == ym2612_eg::kMaxAttenuation);
  CHECK(ssg_curve.release.points.back().out == ym2612_eg::kMaxAttenuation);
}

/// SR = 0 is the case a chained graph could not tell the truth about: with a
/// key-off in the picture the hold is cut short, and without one it is simply
/// flat forever.
void test_an_sr0_held_trace_parks_and_stays_flat() {
  ym2612::OperatorSettings op = worked_example();
  op.sustain_rate = 0;
  const EnvelopeCurve curve = build_envelope_curve(op, 0.0);

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
  const EnvelopeCurve curve = build_envelope_curve(op, 0.0);
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

  const EnvelopeCurve curve = build_envelope_curve(op, 0.0);
  int folds = 0;
  for (const auto &m : curve.held.markers) {
    folds += (m.kind == MarkerKind::SsgFold) ? 1 : 0;
  }
  CHECK(folds >= 3);
  CHECK(!has_marker(curve.held, MarkerKind::KeyOff));
}

// -------------------------------------------------------------- warnings

void test_the_warning_line_names_the_worst_problem_only() {
  ym2612::OperatorSettings frozen = worked_example();
  frozen.attack_rate = 0;
  const EnvelopeCurve frozen_curve = build_envelope_curve(frozen, 0.0);
  CHECK(frozen_curve.warning != nullptr);
  CHECK(std::string(frozen_curve.warning) == "AR=0: never sounds");

  ym2612::OperatorSettings never_loops;
  never_loops.attack_rate = 31;
  never_loops.decay_rate = 15;
  never_loops.sustain_level = 4;
  never_loops.sustain_rate = 0;
  never_loops.release_rate = 7;
  never_loops.ssg_enable = true;
  never_loops.ssg_type_envelope_control = 0;
  const EnvelopeCurve stuck = build_envelope_curve(never_loops, 0.0);
  CHECK(stuck.warning != nullptr);
  CHECK(std::string(stuck.warning) == "never loops (SR=0)");

  ym2612::OperatorSettings slow_attack = never_loops;
  slow_attack.sustain_rate = 8;
  slow_attack.attack_rate = 20;
  const EnvelopeCurve slow = build_envelope_curve(slow_attack, 0.0);
  CHECK(slow.warning != nullptr);
  CHECK(std::string(slow.warning) == "AR<31: non-standard SSG-EG");

  CHECK(build_envelope_curve(worked_example(), 0.0).warning == nullptr);
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

  const EnvelopeCurve fast = build_envelope_curve(op, 0.0);
  CHECK(fast.held.loop_hz > 100.0);
  // The loop, not the release, decides the width.
  CHECK(fast.release_content_ms > fast.span_ms);
  CHECK(fast.span_ms <= fast.held_ms * 3.0);
  // ... and it still gets a readable share of it.
  CHECK(fast.held_ms / fast.span_ms > 0.25);

  // A slow loop already fits, so nothing about it changes.
  ym2612::OperatorSettings slow = op;
  slow.decay_rate = 15;
  const EnvelopeCurve wide = build_envelope_curve(slow, 0.0);
  CHECK(wide.held.loop_hz < 20.0);
  CHECK(wide.held_ms / wide.span_ms > 0.25);
}

/// The same bargain for a plain patch: RR = 2 takes over ten seconds where the
/// attack and decay take three hundred milliseconds, and an axis wide enough
/// for all of it would leave nothing of the part being edited.
void test_a_very_long_release_does_not_crush_the_held_trace() {
  ym2612::OperatorSettings op = worked_example();
  op.release_rate = 2;
  const EnvelopeCurve slow = build_envelope_curve(op, 0.0);
  CHECK(slow.release_truncated); // still falling when the budget ran out
  CHECK(slow.release.points.back().out < ym2612_eg::kMaxAttenuation);
  CHECK(slow.held_ms / slow.span_ms > 0.15);

  // A release that fits is drawn whole, on an axis wide enough for it.
  ym2612::OperatorSettings fits = worked_example();
  fits.release_rate = 6;
  const EnvelopeCurve normal = build_envelope_curve(fits, 0.0);
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

  const EnvelopeCurve c = build_envelope_curve(op, 0.0);
  CHECK(!c.release_truncated);
  CHECK(c.release.points.back().out == ym2612_eg::kMaxAttenuation);
  CHECK(c.release_content_ms > 900.0);
  // The held trace parks; the release is what makes the axis wide.
  CHECK(c.held_parked);
  CHECK(c.span_ms >= c.release_content_ms);
}

} // namespace

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

  test_a_fresh_span_fits_the_content();
  test_the_span_holds_still_inside_the_hysteresis_band();
  test_the_span_never_oscillates_across_a_sweep();
  test_a_sweep_up_and_back_down_does_not_flap();
  test_the_grid_step_divides_the_span_sensibly();

  test_the_worked_examples_decay_lands_on_the_real_millisecond_axis();
  test_the_release_starts_at_full_volume_and_reaches_silence();
  test_ssg_eg_makes_the_release_dramatically_shorter();
  test_an_sr0_held_trace_parks_and_stays_flat();
  test_total_level_moves_the_whole_curve_down();
  test_ssg_enabled_curves_fold();

  test_a_fast_loop_keeps_its_scale();
  test_a_very_long_release_does_not_crush_the_held_trace();
  test_a_slow_release_is_simulated_to_the_end();

  test_the_warning_line_names_the_worst_problem_only();
  test_the_cache_recomputes_only_on_a_real_change();

  std::cout << "envelope_curve_test passed\n";
  return 0;
}
