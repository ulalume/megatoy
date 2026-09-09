#include "../test_check.hpp"
#include "gui/envelope/envelope_handles.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

using namespace ui::envelope;

ym2612::OperatorSettings adsr(int ar, int dr, int sl, int sr, int rr, int tl) {
  ym2612::OperatorSettings op;
  op.attack_rate = static_cast<uint8_t>(ar);
  op.decay_rate = static_cast<uint8_t>(dr);
  op.sustain_level = static_cast<uint8_t>(sl);
  op.sustain_rate = static_cast<uint8_t>(sr);
  op.release_rate = static_cast<uint8_t>(rr);
  op.total_level = static_cast<uint8_t>(tl);
  return op;
}

/// EG_SPEC's worked example: AR=31 TL=0 DR=10 SL=2 SR=5 RR=7, KS=0.
ym2612::OperatorSettings worked_example() { return adsr(31, 10, 2, 5, 7, 0); }

PlotArea plot_over(const EnvelopeCurve &curve) {
  PlotArea plot;
  plot.min = ImVec2(20.0f, 10.0f);
  plot.max = ImVec2(220.0f, 110.0f); // 200 x 100
  plot.span_ms = curve.span_ms;
  return plot;
}

HandleMetrics metrics() { return HandleMetrics{}; }

bool near(double a, double b, double tolerance) {
  return std::fabs(a - b) <= tolerance;
}

// --------------------------------------------------- pixels against values

void test_the_plot_maps_the_curve_onto_its_own_rectangle() {
  PlotArea plot;
  plot.min = ImVec2(20.0f, 10.0f);
  plot.max = ImVec2(220.0f, 110.0f);
  plot.span_ms = 1000.0;

  CHECK(plot.x_of(0.0) == 20.0f);
  CHECK(plot.x_of(500.0) == 120.0f);
  CHECK(plot.x_of(1000.0) == 220.0f);
  // Off either end of the axis stops at the edge rather than smearing past it.
  CHECK(plot.x_of(-10.0) == 20.0f);
  CHECK(plot.x_of(4000.0) == 220.0f);

  CHECK(plot.y_of(0.0) == 10.0f); // full volume is the top
  CHECK(plot.y_of(kFullScale) == 110.0f);

  CHECK(near(plot.ms_per_px(), 5.0, 1e-9));
  CHECK(near(plot.out_per_px(), kFullScale / 100.0, 1e-9));
}

void test_a_drag_that_has_not_moved_asks_for_what_it_grabbed() {
  PlotArea plot;
  plot.min = ImVec2(20.0f, 10.0f);
  plot.max = ImVec2(220.0f, 110.0f);
  plot.span_ms = 1000.0;

  CHECK(dragged_ms(plot, 123.5, 0.0f) == 123.5);
  CHECK(dragged_out(plot, 456.5, 0.0f) == 456.5);

  // And a pointer that has moved lands one axis-worth per pixel away from it,
  // in both directions.
  CHECK(near(dragged_ms(plot, 100.0, 10.0f), 150.0, 1e-9));
  CHECK(near(dragged_ms(plot, 100.0, -10.0f), 50.0, 1e-9));
  CHECK(near(dragged_out(plot, 100.0, 10.0f), 100.0 + kFullScale * 0.1, 1e-9));
}

// ------------------------------------------------------- where they stand

void test_every_handle_stands_where_the_curve_puts_it() {
  const ym2612::OperatorSettings op = worked_example();
  const EnvelopeCurve curve = build_envelope_curve(op);
  const PlotArea plot = plot_over(curve);
  const EnvelopeHandles handles = handle_layout(curve, plot, false, metrics());

  const EnvelopeHandle &attack = handles.items[kAttackHandle];
  CHECK(attack.shown);
  // The attack starts at zero, so its length and its place on the axis are
  // the same number.
  CHECK(attack.ms == curve.attack_end_ms);
  CHECK(attack.out == curve.peak_out);

  const EnvelopeHandle &decay = handles.items[kDecayHandle];
  CHECK(decay.shown);
  // The decay's own length, not where it lands on the axis.
  CHECK(near(decay.ms, curve.decay_end_ms - curve.attack_end_ms, 1e-9));
  CHECK(decay.out == curve.sustain_out);
  CHECK(near(decay.pos.x, plot.x_of(curve.decay_end_ms), 0.001));

  const EnvelopeHandle &sustain = handles.items[kSustainHandle];
  CHECK(sustain.shown);
  const double probe = sustain_probe_ms(curve, plot.span_ms);
  CHECK(near(probe, (curve.decay_end_ms + curve.span_ms) * 0.5, 1e-9));
  CHECK(near(sustain.ms, probe - curve.decay_end_ms, 1e-9));
  // Read off the trace that is drawn rather than off a closed form.
  CHECK(near(sustain.out, curve_out_at_ms(curve.held, probe), 1e-9));

  const EnvelopeHandle &release = handles.items[kReleaseHandle];
  CHECK(release.shown);
  CHECK(release.ms == curve.release_content_ms);
  CHECK(release.out == kFullScale);
}

void test_the_whole_dot_stays_inside_the_plot() {
  // TL 0 puts the peak on the top edge and AR 31 puts it on the left one.
  const EnvelopeCurve curve = build_envelope_curve(worked_example());
  const PlotArea plot = plot_over(curve);
  const HandleMetrics sizes = metrics();
  const EnvelopeHandles handles = handle_layout(curve, plot, false, sizes);

  for (int i = 0; i < kHandleCount; ++i) {
    const EnvelopeHandle &handle = handles.items[i];
    if (!handle.shown) {
      continue;
    }
    CHECK(handle.pos.x >= plot.min.x + sizes.radius);
    CHECK(handle.pos.x <= plot.max.x - sizes.radius);
    CHECK(handle.pos.y >= plot.min.y + sizes.radius);
    CHECK(handle.pos.y <= plot.max.y - sizes.radius);
  }
}

// ------------------------------------------------- what is not drawn at all

void test_ssg_eg_leaves_no_handle_behind() {
  ym2612::OperatorSettings op = worked_example();
  op.ssg_enable = true;
  op.ssg_type_envelope_control = 4;
  const EnvelopeCurve curve = build_envelope_curve(op);
  const EnvelopeHandles handles =
      handle_layout(curve, plot_over(curve), true, metrics());
  for (int i = 0; i < kHandleCount; ++i) {
    CHECK(!handles.items[i].shown);
  }
}

void test_a_knee_that_stands_on_the_peak_is_not_offered() {
  // Sustain level 0 leaves the decay no length: the knee would be the peak.
  const auto op = adsr(31, 10, 0, 5, 7, 20);
  const EnvelopeCurve curve = build_envelope_curve(op);
  const PlotArea plot = plot_over(curve);
  const EnvelopeHandles handles = handle_layout(curve, plot, false, metrics());
  CHECK(!handles.items[kDecayHandle].shown);
  CHECK(handles.items[kAttackHandle].shown);

  // A level the eye can separate from the peak brings it back.
  const EnvelopeCurve apart = build_envelope_curve(adsr(31, 10, 4, 5, 7, 20));
  const EnvelopeHandles offered =
      handle_layout(apart, plot_over(apart), false, metrics());
  CHECK(offered.items[kDecayHandle].shown);
}

void test_the_pointer_answers_the_handle_it_is_nearest() {
  const auto op = adsr(31, 10, 4, 5, 7, 20);
  const EnvelopeCurve curve = build_envelope_curve(op);
  const PlotArea plot = plot_over(curve);
  const EnvelopeHandles handles = handle_layout(curve, plot, false, metrics());
  CHECK(handles.items[kAttackHandle].shown);
  CHECK(handles.items[kDecayHandle].shown);

  const ImVec2 peak = handles.items[kAttackHandle].pos;
  const ImVec2 knee = handles.items[kDecayHandle].pos;
  CHECK(nearest_handle(handles, peak) == kAttackHandle);
  CHECK(nearest_handle(handles, knee) == kDecayHandle);
  // Just off the peak, still its own.
  CHECK(nearest_handle(handles, ImVec2(peak.x + 1.0f, peak.y)) ==
        kAttackHandle);
  // Past every grab box, nobody answers.
  CHECK(nearest_handle(handles, ImVec2(plot.max.x, plot.min.y)) ==
        kHandleCount);
}

void test_a_sliver_of_sustain_carries_no_handle() {
  // SL 15 leaves the sustain a few units above the floor: on a 100 px plot
  // that is three, and there is nothing to drag it through.
  const ym2612::OperatorSettings op = adsr(31, 10, 15, 5, 7, 0);
  const EnvelopeCurve curve = build_envelope_curve(op);
  const EnvelopeHandles handles =
      handle_layout(curve, plot_over(curve), false, metrics());
  CHECK(!handles.items[kSustainHandle].shown);
  CHECK(handles.items[kDecayHandle].shown);
}

void test_a_release_that_outran_the_simulation_has_no_end_to_grab() {
  // RR 0 to 3 are longer than the release is ever simulated for, so what the
  // trace ends at is the budget rather than the release.
  for (int rr = 0; rr <= 3; ++rr) {
    const EnvelopeCurve curve = build_envelope_curve(adsr(31, 10, 2, 5, rr, 0));
    const EnvelopeHandles handles =
        handle_layout(curve, plot_over(curve), false, metrics());
    CHECK(!handles.items[kReleaseHandle].shown);
  }
  // A TL that has already taken the trace to the bottom of the graph does not
  // make the budget any less the reason it stopped.
  const EnvelopeCurve quiet = build_envelope_curve(adsr(31, 10, 2, 5, 3, 64));
  CHECK(!quiet.release_truncated);
  CHECK(!handle_layout(quiet, plot_over(quiet), false, metrics())
             .items[kReleaseHandle]
             .shown);

  const EnvelopeCurve curve = build_envelope_curve(adsr(31, 10, 2, 5, 4, 64));
  CHECK(handle_layout(curve, plot_over(curve), false, metrics())
            .items[kReleaseHandle]
            .shown);
}

// ------------------------------------------------------ back to a register

/**
 * The property the whole interaction rests on: asking a handle's own solver
 * about the place that handle already stands answers with a curve drawn in
 * exactly that place.
 *
 * Not always with the register it came from -- several rates can be the same
 * length, and every sustain level past the bottom of the scale is the bottom
 * of the scale -- but never with one that would move the curve out from under
 * the pointer.
 */
void test_reading_a_handle_back_leaves_the_curve_where_it_is() {
  for (int ks : {0, 3}) {
    for (int ar : {8, 20, 31}) {
      for (int dr : {4, 16, 28}) {
        for (int sl : {0, 3, 9, 14}) {
          for (int sr : {0, 7, 18, 31}) {
            for (int rr : {5, 11, 15}) {
              for (int tl : {0, 17, 64, 120}) {
                ym2612::OperatorSettings op = adsr(ar, dr, sl, sr, rr, tl);
                op.key_scale = static_cast<uint8_t>(ks);
                const EnvelopeCurve curve = build_envelope_curve(op);
                const PlotArea plot = plot_over(curve);
                const EnvelopeHandles handles =
                    handle_layout(curve, plot, false, metrics());
                const auto solved_curve = [&](ym2612::OperatorField field,
                                              double target, double elapsed) {
                  ym2612::OperatorSettings solved = op;
                  ym2612::write_operator_field(
                      solved, field,
                      solve_operator_field(op, field, target, elapsed));
                  return build_envelope_curve(solved);
                };

                const EnvelopeHandle &attack = handles.items[kAttackHandle];
                if (attack.shown) {
                  CHECK(solved_curve(ym2612::OperatorField::AttackRate,
                                     attack.ms, 0.0)
                            .attack_end_ms == curve.attack_end_ms);
                  // TL is the top seven bits of the level it sets, so it is
                  // the one field that always comes back as itself.
                  CHECK(solve_operator_field(op,
                                             ym2612::OperatorField::TotalLevel,
                                             attack.out, 0.0) == tl);
                }
                const EnvelopeHandle &decay = handles.items[kDecayHandle];
                if (decay.shown) {
                  const EnvelopeCurve rate = solved_curve(
                      ym2612::OperatorField::DecayRate, decay.ms, 0.0);
                  CHECK(near(rate.decay_end_ms - rate.attack_end_ms, decay.ms,
                             1e-6));
                  CHECK(solved_curve(ym2612::OperatorField::SustainLevel,
                                     decay.out, 0.0)
                            .sustain_out == curve.sustain_out);
                }
                const EnvelopeHandle &sustain = handles.items[kSustainHandle];
                if (sustain.shown) {
                  const double at = sustain_probe_ms(curve, plot.span_ms);
                  const EnvelopeCurve rate = solved_curve(
                      ym2612::OperatorField::SustainRate, sustain.out,
                      sustain.ms);
                  CHECK(near(curve_out_at_ms(rate.held, at), sustain.out,
                             plot.out_per_px()));
                }
                const EnvelopeHandle &release = handles.items[kReleaseHandle];
                if (release.shown) {
                  CHECK(solved_curve(ym2612::OperatorField::ReleaseRate,
                                     release.ms, 0.0)
                            .release_content_ms == release.ms);
                }
              }
            }
          }
        }
      }
    }
  }
}

/// Dragging one way makes the phase longer and the level quieter, all the way
/// to the ends of the ranges.
void test_a_drag_goes_where_it_is_pointed() {
  const ym2612::OperatorSettings op = adsr(20, 14, 6, 10, 8, 32);
  const EnvelopeCurve curve = build_envelope_curve(op);
  const PlotArea plot = plot_over(curve);
  const EnvelopeHandles handles = handle_layout(curve, plot, false, metrics());

  const EnvelopeHandle &attack = handles.items[kAttackHandle];
  CHECK(solve_operator_field(op, ym2612::OperatorField::AttackRate,
                             dragged_ms(plot, attack.ms, 8.0f), 0.0) < 20);
  CHECK(solve_operator_field(op, ym2612::OperatorField::AttackRate,
                             dragged_ms(plot, attack.ms, -8.0f), 0.0) > 20);
  // Dragging the peak down is a quieter operator.
  CHECK(solve_operator_field(op, ym2612::OperatorField::TotalLevel,
                             dragged_out(plot, attack.out, 6.0f), 0.0) > 32);

  // Dragging the end of the release left is a quicker release, right a slower
  // one.
  const EnvelopeHandle &release = handles.items[kReleaseHandle];
  CHECK(solve_operator_field(op, ym2612::OperatorField::ReleaseRate,
                             dragged_ms(plot, release.ms, -40.0f), 0.0) > 8);
  CHECK(solve_operator_field(op, ym2612::OperatorField::ReleaseRate,
                             dragged_ms(plot, release.ms, 40.0f), 0.0) < 8);
  // RR 14 and 15 are the same length, so the quickest release a drag arrives
  // at is 14.
  CHECK(solve_operator_field(op, ym2612::OperatorField::ReleaseRate, 1.0,
                             0.0) == 14);

  // The top of the sustain line is SR 0, the rate that holds.
  const EnvelopeHandle &sustain = handles.items[kSustainHandle];
  CHECK(solve_operator_field(op, ym2612::OperatorField::SustainRate,
                             static_cast<double>(curve.sustain_out),
                             sustain.ms) == 0);
}

} // namespace

int main() {
  test_the_plot_maps_the_curve_onto_its_own_rectangle();
  test_a_drag_that_has_not_moved_asks_for_what_it_grabbed();

  test_every_handle_stands_where_the_curve_puts_it();
  test_the_whole_dot_stays_inside_the_plot();

  test_ssg_eg_leaves_no_handle_behind();
  test_a_sliver_of_sustain_carries_no_handle();
  test_a_release_that_outran_the_simulation_has_no_end_to_grab();

  test_reading_a_handle_back_leaves_the_curve_where_it_is();
  test_a_drag_goes_where_it_is_pointed();

  std::cout << "envelope_handles_test passed\n";
  return 0;
}
