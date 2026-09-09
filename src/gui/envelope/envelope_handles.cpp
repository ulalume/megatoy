#include "gui/envelope/envelope_handles.hpp"

#include <algorithm>

namespace ui::envelope {
namespace {

/// The whole dot inside the plot: a handle in a corner is still a dot rather
/// than a quarter of one, and its grab box is not half off the graph.
ImVec2 inside(const PlotArea &plot, ImVec2 pos, float radius) {
  return ImVec2(std::clamp(pos.x, plot.min.x + radius, plot.max.x - radius),
                std::clamp(pos.y, plot.min.y + radius, plot.max.y - radius));
}

/// The first instant on `trace` at or past `level`, looked for between `from`
/// and `to`. The phases are monotone, so this is where the drawn line arrives
/// at a level the registers put somewhere else.
double first_time_at_level(const ym2612_eg::CurveResult &trace, double level,
                           double from_ms, double to_ms) {
  for (const auto &point : trace.points) {
    if (point.ms < from_ms) {
      continue;
    }
    if (point.ms > to_ms) {
      break;
    }
    if (static_cast<double>(point.out) >= level) {
      return point.ms;
    }
  }
  return to_ms;
}

} // namespace

double sustain_probe_ms(const EnvelopeCurve &curve, double span_ms) {
  const double start = std::max(curve.decay_end_ms, 0.0);
  return start + (span_ms - start) * 0.5;
}

EnvelopeHandles handle_layout(const EnvelopeCurve &curve, const PlotArea &plot,
                              bool ssg_enabled, const HandleMetrics &metrics) {
  EnvelopeHandles out;
  out.plot = plot;
  out.metrics = metrics;
  // An SSG-EG envelope is not four parameters laid end to end: the trace folds,
  // and no point on it stands for one register.
  if (ssg_enabled) {
    return out;
  }

  const double span = plot.span_ms;
  // `ms` is what the solver is asked about; `at_ms` is where on the axis that
  // puts the dot. They differ wherever a phase does not start at zero.
  const auto place = [&](HandleIndex index, double ms, double at_ms,
                         double out_att, double ms_per_drawn = 1.0,
                         bool parked = false, double anchor_ms = 0.0,
                         double anchor_out = 0.0) {
    EnvelopeHandle &handle = out.items[index];
    handle.shown = true;
    handle.ms = ms;
    handle.out = out_att;
    handle.at_ms = at_ms;
    handle.anchor_ms = anchor_ms;
    handle.anchor_out = anchor_out;
    handle.parked = parked;
    handle.ms_per_drawn = ms_per_drawn;
    handle.pos = inside(plot, plot.at(at_ms, out_att), metrics.radius);
  };

  // The peak: TotalLevel is the level it stands at, AttackRate the time it
  // took to get there.
  if (curve.attack_end_ms >= 0.0 && curve.attack_end_ms <= span) {
    place(kAttackHandle, curve.attack_end_ms, curve.attack_end_ms,
          curve.peak_out);
  }

  // The knee, whose time is the decay's own length rather than where it falls
  // on the axis. A decay rate of 0 never reaches the sustain level and a slow
  // one can end past the axis: either way the knee waits at the right-hand
  // edge, on the line it would leave, and pulling it in is what gives the
  // envelope a decay at all. A sustain level of 0 is the one case with
  // nothing to point at -- the knee stands on the peak, and a drag there
  // would be guesswork about which of the two was meant.
  if (curve.attack_end_ms >= 0.0 && curve.attack_end_ms <= span) {
    const bool knee_on_axis =
        curve.decay_end_ms >= 0.0 && curve.decay_end_ms <= span;
    const double decay_ms =
        (knee_on_axis ? curve.decay_end_ms : span) - curve.attack_end_ms;
    const double out_att = knee_on_axis ? curve.sustain_out
                                        : curve_out_at_ms(curve.held, span);
    // Where the eye finds the knee, which comes before the decay's own end
    // whenever the output saturates on the way down: TL lifts the whole
    // envelope, so a high sustain level is already at the floor of the graph
    // while the attenuation still has ground to cover.
    const double at_ms =
        first_time_at_level(curve.held, out_att, curve.attack_end_ms,
                            knee_on_axis ? curve.decay_end_ms : span);
    const double drawn_ms = at_ms - curve.attack_end_ms;
    const ImVec2 peak = plot.at(curve.attack_end_ms, curve.peak_out);
    const ImVec2 knee = plot.at(at_ms, out_att);
    if (std::abs(knee.x - peak.x) >= metrics.radius ||
        std::abs(knee.y - peak.y) >= metrics.radius) {
      place(kDecayHandle, decay_ms, at_ms, out_att,
            drawn_ms > 0.0 ? decay_ms / drawn_ms : 1.0, !knee_on_axis,
            curve.attack_end_ms, curve.peak_out);
    } else {
      // A sustain level of 0 puts the knee on the peak, where a dot would be
      // the peak's. It stands just clear of it instead: the decay is what
      // pulling it away from there gives the envelope.
      const double clear_ms =
          curve.attack_end_ms + metrics.grab * plot.ms_per_px();
      place(kDecayHandle, clear_ms - curve.attack_end_ms, clear_ms,
            curve_out_at_ms(curve.held, clear_ms), 1.0, true,
            curve.attack_end_ms, curve.peak_out);
    }
  }

  // The sustain has no corner to grab, so the handle sits at a fixed instant
  // along it and carries the level the trace is actually drawn at there. Too
  // thin or too narrow and there is nothing to drag it through.
  if (curve.decay_end_ms >= 0.0) {
    const double at_ms = sustain_probe_ms(curve, span);
    const float room_x = plot.x_of(span) - plot.x_of(curve.decay_end_ms);
    const float room_y = plot.y_of(kFullScale) - plot.y_of(curve.sustain_out);
    if (at_ms > curve.decay_end_ms && room_x >= metrics.min_sustain_width &&
        room_y >= metrics.min_sustain_height) {
      const double out_att = curve_out_at_ms(curve.held, at_ms);
      place(kSustainHandle, at_ms - curve.decay_end_ms, at_ms, out_att,
            1.0, false, curve.decay_end_ms, curve.sustain_out);
    }
  }

  // Where the release reaches the floor of the graph, which comes before the
  // trace's own end whenever TL lifts the envelope: the output saturates
  // while the attenuation still has ground to cover. The dot stands on what
  // the eye sees and the solver is told about the release behind it. One that
  // outran the simulation ends where the budget did rather than where the
  // release does, so the budget is what has to be tested.
  if (curve.release_content_ms > 0.0 &&
      curve.release_content_ms < release_max_ms() * 0.999) {
    double floor_ms = curve.release_content_ms;
    for (const auto &point : curve.release.points) {
      if (point.out >= ym2612_eg::kMaxAttenuation) {
        floor_ms = point.ms;
        break;
      }
    }
    const double scale =
        floor_ms > 0.0 ? curve.release_content_ms / floor_ms : 1.0;
    // A release that reaches the floor past the right-hand edge is still a
    // line on the graph: the dot waits on it at the edge, where tilting it
    // is what brings the end back into view.
    const bool ends_on_axis = floor_ms <= span;
    const double at_ms = ends_on_axis ? floor_ms : span;
    place(kReleaseHandle, curve.release_content_ms, at_ms,
          ends_on_axis ? kFullScale : curve_out_at_ms(curve.release, at_ms),
          scale, !ends_on_axis, 0.0, curve.peak_out);
  }

  return out;
}

HandleIndex nearest_handle(const EnvelopeHandles &handles, ImVec2 pos) {
  HandleIndex nearest = kHandleCount;
  float best = handles.metrics.grab * handles.metrics.grab;
  for (int i = 0; i < kHandleCount; ++i) {
    const EnvelopeHandle &item = handles.items[i];
    if (!item.shown) {
      continue;
    }
    const float dx = item.pos.x - pos.x;
    const float dy = item.pos.y - pos.y;
    const float distance = dx * dx + dy * dy;
    if (distance <= best) {
      best = distance;
      nearest = static_cast<HandleIndex>(i);
    }
  }
  return nearest;
}

double dragged_ms(const PlotArea &plot, double grabbed_ms, float moved_px,
                  double ms_per_drawn) {
  return grabbed_ms +
         static_cast<double>(moved_px) * plot.ms_per_px() * ms_per_drawn;
}

double dragged_out(const PlotArea &plot, double grabbed_out, float moved_px) {
  return grabbed_out + static_cast<double>(moved_px) * plot.out_per_px();
}

int solve_operator_field(const ym2612::OperatorSettings &op,
                         ym2612::OperatorField field, double target,
                         double elapsed_ms) {
  namespace graph = ym2612_eg::graph;
  const ym2612_eg::OperatorParams params = to_operator_params(op);
  const ym2612_eg::NotePitch pitch = reference_pitch();
  switch (field) {
  case ym2612::OperatorField::AttackRate:
    return graph::solve_attack_rate(params, pitch, target);
  case ym2612::OperatorField::DecayRate:
    return graph::solve_decay_rate(params, pitch, target);
  case ym2612::OperatorField::ReleaseRate:
    return graph::solve_release_rate(params, pitch, target);
  case ym2612::OperatorField::TotalLevel:
    return graph::solve_total_level(target);
  case ym2612::OperatorField::SustainLevel:
    return graph::solve_sustain_level(params, target);
  case ym2612::OperatorField::SustainRate:
    return graph::solve_sustain_rate(params, pitch, elapsed_ms, target);
  default:
    return ym2612::read_operator_field(op, field);
  }
}

} // namespace ui::envelope
