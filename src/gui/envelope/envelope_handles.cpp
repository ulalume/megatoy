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
                         double out_att) {
    EnvelopeHandle &handle = out.items[index];
    handle.shown = true;
    handle.ms = ms;
    handle.out = out_att;
    handle.pos = inside(plot, plot.at(at_ms, out_att), metrics.radius);
  };

  // The peak: TotalLevel is the level it stands at, AttackRate the time it
  // took to get there.
  if (curve.attack_end_ms >= 0.0 && curve.attack_end_ms <= span) {
    place(kAttackHandle, curve.attack_end_ms, curve.attack_end_ms,
          curve.peak_out);
  }

  // The knee, whose time is the decay's own length rather than where it falls
  // on the axis. A sustain level of 0 leaves no decay to point at: the knee
  // stands on the peak, where a dot would be the peak's and a drag would be
  // guesswork about which of the two was meant.
  if (curve.attack_end_ms >= 0.0 && curve.decay_end_ms >= 0.0 &&
      curve.decay_end_ms <= span) {
    const ImVec2 peak = plot.at(curve.attack_end_ms, curve.peak_out);
    const ImVec2 knee = plot.at(curve.decay_end_ms, curve.sustain_out);
    if (std::abs(knee.x - peak.x) >= metrics.radius ||
        std::abs(knee.y - peak.y) >= metrics.radius) {
      place(kDecayHandle, curve.decay_end_ms - curve.attack_end_ms,
            curve.decay_end_ms, curve.sustain_out);
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
      place(kSustainHandle, at_ms - curve.decay_end_ms, at_ms, out_att);
    }
  }

  // Where the release reaches the floor. One that outran the simulation ends
  // where the budget did rather than where the release does -- and it can
  // reach the bottom of the graph long before that, so the budget is what has
  // to be tested.
  if (curve.release_content_ms > 0.0 && curve.release_content_ms <= span &&
      curve.release_content_ms < release_max_ms() * 0.999) {
    place(kReleaseHandle, curve.release_content_ms, curve.release_content_ms,
          kFullScale);
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

double dragged_ms(const PlotArea &plot, double grabbed_ms, float moved_px) {
  return grabbed_ms + static_cast<double>(moved_px) * plot.ms_per_px();
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
