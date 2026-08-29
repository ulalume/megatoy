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

// --------------------------------------------------------- the gate policy

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

  const Gate gate = choose_gate(held, to_operator_params(op));
  CHECK(gate.gate_ms > decay_end);
  // A quarter of the held time is sustain, so SR always has something to
  // highlight.
  const double sustain_share = (gate.gate_ms - decay_end) / gate.gate_ms;
  CHECK(near_rel(sustain_share, 0.25, 0.02));
  // The 27 s sustain decay of this patch must not set the width.
  CHECK(gate.gate_ms <= 2000.0);
  CHECK(gate.max_ms > gate.gate_ms);
}

void test_an_sr0_patch_holds_past_the_park() {
  ym2612::OperatorSettings op = worked_example();
  op.sustain_rate = 0; // parks at the sustain level and stays there

  const CurveResult held = probe(op);
  CHECK(std::isfinite(held.park_ms));
  const Gate gate = choose_gate(held, to_operator_params(op));
  // The flat part has to be visible, not a single pixel at the right edge.
  CHECK(gate.gate_ms > held.park_ms * 1.2);
  CHECK(gate.gate_ms <= 2000.0);
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
  const Gate gate = choose_gate(held, to_operator_params(op));
  const double period_ms = 1000.0 / held.loop_hz;
  const double periods = gate.gate_ms / period_ms;
  CHECK(periods >= 3.0);
  CHECK(periods <= 4.0);
}

void test_a_frozen_attack_still_produces_a_usable_window() {
  ym2612::OperatorSettings op = worked_example();
  op.attack_rate = 0; // never sounds; parks immediately

  const CurveResult held = probe(op);
  const Gate gate = choose_gate(held, to_operator_params(op));
  CHECK(gate.gate_ms >= 50.0);
  CHECK(gate.max_ms >= gate.gate_ms + 200.0);
}

void test_the_gate_is_bounded_across_a_sweep() {
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
          const Gate gate = choose_gate(probe(op), to_operator_params(op));
          CHECK(gate.gate_ms >= 50.0);
          CHECK(gate.gate_ms <= 10000.0);
          CHECK(gate.max_ms > gate.gate_ms);
          CHECK(gate.max_ms <= gate.gate_ms + 4000.0);
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

void test_the_worked_examples_decay_lands_on_the_real_millisecond_axis() {
  const EnvelopeCurve curve = build_envelope_curve(worked_example(), 0.0);
  // EG_SPEC: the decay of AR=31 TL=0 DR=10 SL=2 ends on tick 5440 = 306.4 ms.
  CHECK(curve.decay_end_ms > 0.0);
  CHECK(near_rel(curve.decay_end_ms, 306.4, 0.02));

  // Everything the graph draws is on that same axis.
  CHECK(curve.attack_end_ms >= 0.0);
  CHECK(curve.attack_end_ms < curve.decay_end_ms);
  CHECK(curve.key_off_ms > curve.decay_end_ms);
  CHECK(near_rel(curve.key_off_ms, curve.gate_ms, 0.01));
  CHECK(curve.content_ms > curve.key_off_ms);
  CHECK(curve.span_ms >= curve.content_ms);
  CHECK(!curve.curve.points.empty());
  CHECK(curve.warning == nullptr);

  // AR = 31 is an instant attack, so the curve starts at full volume; it is
  // monotonic in time and ends silent either way.
  CHECK(curve.curve.points.front().ms == 0.0f);
  CHECK(curve.curve.points.front().out == 0);
  CHECK(curve.curve.points.back().out == ym2612_eg::kMaxAttenuation);
  CHECK(curve.peak_out == 0); // TL = 0
  CHECK(curve.sustain_out == 64);
  double previous = -1.0;
  bool reached_peak = false;
  for (const auto &p : curve.curve.points) {
    CHECK(p.ms >= previous);
    previous = p.ms;
    reached_peak |= (p.out == 0);
  }
  CHECK(reached_peak);
}

void test_total_level_moves_the_whole_curve_down() {
  ym2612::OperatorSettings op = worked_example();
  op.total_level = 32;
  const EnvelopeCurve curve = build_envelope_curve(op, 0.0);
  CHECK(curve.peak_out == 32 * 8);
  CHECK(curve.sustain_out == 64 + 32 * 8);
  for (const auto &p : curve.curve.points) {
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
  for (const auto &m : curve.curve.markers) {
    folds += (m.kind == MarkerKind::SsgFold) ? 1 : 0;
  }
  CHECK(folds >= 3);
  CHECK(curve.key_off_ms > 0.0);
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

} // namespace

int main() {
  test_registers_map_straight_through();
  test_ssg_bits_are_packed_the_way_the_chip_wants_them();
  test_only_envelope_registers_count_as_a_change();

  test_a_normal_patch_gets_a_visible_sustain();
  test_an_sr0_patch_holds_past_the_park();
  test_an_ssg_loop_shows_a_few_periods();
  test_a_frozen_attack_still_produces_a_usable_window();
  test_the_gate_is_bounded_across_a_sweep();

  test_a_fresh_span_fits_the_content();
  test_the_span_holds_still_inside_the_hysteresis_band();
  test_the_span_never_oscillates_across_a_sweep();
  test_a_sweep_up_and_back_down_does_not_flap();
  test_the_grid_step_divides_the_span_sensibly();

  test_the_worked_examples_decay_lands_on_the_real_millisecond_axis();
  test_total_level_moves_the_whole_curve_down();
  test_ssg_enabled_curves_fold();

  test_the_warning_line_names_the_worst_problem_only();
  test_the_cache_recomputes_only_on_a_real_change();

  std::cout << "envelope_curve_test passed\n";
  return 0;
}
