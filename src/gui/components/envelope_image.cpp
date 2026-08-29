#include "envelope_image.hpp"

#include "app_state.hpp"
#include "common.hpp"
#include "gui/envelope/envelope_curve.hpp"
#include "gui/ui_scale.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <imgui.h>
#include <limits>
#include <unordered_map>
#include <vector>

/**
 * The envelope graph: two independent traces on one elapsed-time axis.
 *
 * Everything drawn here comes from ym2612_eg by way of gui/envelope: the
 * polylines are the chip's own envelope, the x axis is real milliseconds and
 * the y axis is real attenuation. This file decides nothing about the shapes --
 * it only turns them into pixels, colours the segment the user is dragging,
 * and prints the one warning the policy layer picked.
 *
 * The held envelope (attack, decay, sustain, key never released) is a LINE;
 * the release, which starts from full volume at x = 0, is a translucent FILLED
 * AREA. They start from different events and are deliberately not chained:
 * chaining them would mean inventing a key-off instant, which would cut the
 * sustain short at an arbitrary time and start the release from a level that
 * fiction produced. The two-shape language is the one megatoy's graph has
 * always used.
 */

namespace ui {
namespace {

using ui::envelope::EnvelopeCurve;
using ui::envelope::EnvelopeCurveCache;

/// Attenuation at the bottom of the graph; 0 (full volume) is at the top.
constexpr double kFullScale = static_cast<double>(ym2612_eg::kMaxAttenuation);

/// The wash under the release. It covers one falling edge rather than the
/// whole shape, so it can be strong enough to read as an area on its own --
/// this is the alpha the release triangle has always been drawn at.
constexpr float kFillAlpha = 0.4f;
/// The warning line is a footnote, not an alert.
constexpr float kWarningAlpha = 0.6f;

/// Which parameter owns a stretch of the curve. The slider highlight follows
/// these, so the boundaries are the curve's own markers rather than anything
/// re-derived from the registers.
enum SegmentIndex {
  kAttack = 0,
  kDecay = 1,
  kSustain = 2,
  kRelease = 3,
  kSegmentCount = 4,
};

/**
 * The curve has to survive between frames -- rebuilding it on every one would
 * run the simulator four times a frame for nothing -- and there is one per
 * operator. operator_editor pushes the slot onto the ID stack, so the widget's
 * own ID separates them and the map never holds more than four entries.
 */
EnvelopeCurveCache &cache_for(ImGuiID id) {
  static std::unordered_map<ImGuiID, EnvelopeCurveCache> caches;
  return caches[id];
}

ImU32 color_from_slider_state(
    const UIState::EnvelopeState::SliderState &state) {
  switch (state) {
  case UIState::EnvelopeState::SliderState::None:
    return ImGui::GetColorU32(ImGuiCol_Text);
  case UIState::EnvelopeState::SliderState::Hover:
    return ImGui::GetColorU32(ImGuiCol_FrameBgActive);
  case UIState::EnvelopeState::SliderState::Active:
    return ImGui::GetColorU32(ImGuiCol_FrameBgActive);
  }
  // Default case to prevent warning
  return ImGui::GetColorU32(ImGuiCol_FrameBg);
}

/**
 * The instants the held line changes hands.
 *
 * A marker is negative when the segment never happened, and then the segment
 * before it simply runs on: an AR = 0 patch is attack forever, and a DR = 0
 * patch decays without ever reaching sustain. Each boundary is therefore
 * pinned to the one before it. There is no key-off on this line, so the
 * sustain owns everything past the decay -- including the stretch continued
 * out to the right edge.
 */
struct SegmentBounds {
  double attack_end = 0.0;
  double decay_end = 0.0;

  int index_at(double ms) const {
    if (ms < attack_end) {
      return kAttack;
    }
    if (ms < decay_end) {
      return kDecay;
    }
    return kSustain;
  }
};

SegmentBounds segment_bounds(const EnvelopeCurve &curve) {
  constexpr double kNever = std::numeric_limits<double>::infinity();
  SegmentBounds bounds;
  bounds.attack_end =
      curve.attack_end_ms >= 0.0 ? curve.attack_end_ms : kNever;
  bounds.decay_end =
      std::max(curve.decay_end_ms >= 0.0 ? curve.decay_end_ms : kNever,
               bounds.attack_end);
  return bounds;
}

/// Maps the curve's own units onto the canvas: ms across, attenuation down.
struct PlotArea {
  ImVec2 min;
  ImVec2 max;
  double span_ms = 1.0;

  float width() const { return std::max(max.x - min.x, 1.0f); }
  float height() const { return std::max(max.y - min.y, 1.0f); }

  float x_of(double ms) const {
    const double t = std::clamp(ms / span_ms, 0.0, 1.0);
    return min.x + static_cast<float>(t) * width();
  }
  float y_of(double out) const {
    const double t = std::clamp(out / kFullScale, 0.0, 1.0);
    return min.y + static_cast<float>(t) * height();
  }
  ImVec2 at(double ms, double out) const { return ImVec2(x_of(ms), y_of(out)); }
};

void format_ms(char (&out)[16], double ms) {
  std::snprintf(out, sizeof(out), "%dms", static_cast<int>(ms + 0.5));
}

/**
 * The time grid, labelled every nth line.
 *
 * The graph is about as wide as six vertical sliders, so on most axes the six
 * divisions do not all have room for their text. Labelling every nth line --
 * n being however many it takes for the widest label to fit between two of
 * them -- keeps the labels evenly spaced, which reads as an axis; labelling
 * whichever ones happen to fit gives 0, 250, 750 and reads as a mistake. The
 * lines themselves are all drawn.
 */
void draw_time_grid(ImDrawList *draw_list, const PlotArea &plot,
                    float label_baseline) {
  const ImU32 grid_color = ImGui::GetColorU32(ImGuiCol_Separator);
  const ImU32 label_color =
      color_with_alpha(ImGui::GetColorU32(ImGuiCol_Text), kWarningAlpha);
  const double step = ui::envelope::grid_step_ms(plot.span_ms);
  const int lines = static_cast<int>(plot.span_ms / step + 1e-6);

  char widest[16];
  format_ms(widest, step * lines);
  const float needed = ImGui::CalcTextSize(widest).x + ui::scale::px(6.0f);
  const float pitch = plot.width() * static_cast<float>(step / plot.span_ms);
  const int label_every =
      std::max(1, static_cast<int>(std::ceil(needed / std::max(pitch, 1.0f))));

  for (int i = 0; i <= lines; ++i) {
    const double ms = step * i;
    const float x = plot.x_of(ms);
    draw_list->AddLine(ImVec2(x, plot.min.y), ImVec2(x, plot.max.y),
                       grid_color);
    if (i % label_every != 0) {
      continue;
    }
    char label[16];
    format_ms(label, ms);
    if (x + ImGui::CalcTextSize(label).x > plot.max.x) {
      continue; // would hang off the right edge
    }
    draw_list->AddText(ImVec2(x, label_baseline), label_color, label);
  }

  // Full volume, half attenuation, silence.
  for (int i = 0; i <= 2; ++i) {
    const float y = plot.y_of(kFullScale * 0.5 * i);
    draw_list->AddLine(ImVec2(plot.min.x, y), ImVec2(plot.max.x, y),
                       grid_color);
  }
}

/**
 * One more piece of curve past the last simulated point.
 *
 * A trace can stop before the right edge: the held envelope stops the moment
 * it comes to rest, and a slow release can outlast its budget. Either way the
 * line must not stop in mid-air.
 *
 * `flat` means the envelope came to rest -- an SR = 0 hold, a frozen attack --
 * so it is continued horizontally, which is exactly what the chip would do.
 * Otherwise it is continued along its final slope; post-attack segments are
 * linear in attenuation, so that is as accurate as the rest of the curve. A
 * moving held envelope is simulated all the way to the axis edge, so in
 * practice that second case is the release's alone.
 */
bool extrapolated_tail(const std::vector<ym2612_eg::CurvePoint> &points,
                       bool flat, double span_ms, double &end_ms,
                       double &end_out) {
  if (points.empty()) {
    return false;
  }
  const ym2612_eg::CurvePoint &last = points.back();
  if (last.ms >= span_ms) {
    return false;
  }
  end_ms = span_ms;
  if (flat) {
    end_out = last.out;
    return true;
  }
  if (points.size() < 2) {
    return false;
  }
  // sample_curve() closes every polyline with a point at the end of the
  // simulated span, which can repeat the level already there -- a final edge
  // that is both very short and perfectly flat. Extrapolating a whole graph
  // width from that would draw a flat line across an envelope that is plainly
  // still moving, so measure the slope from the last point at a different
  // level instead.
  size_t base = points.size() - 1;
  while (base > 0 && points[base - 1].out == last.out) {
    --base;
  }
  if (base == 0) {
    end_out = last.out; // the whole trace sits at one level
    return true;
  }
  const ym2612_eg::CurvePoint &previous = points[base - 1];
  const double dt = static_cast<double>(last.ms) - previous.ms;
  const double slope =
      dt > 0.0 ? (static_cast<double>(last.out) - previous.out) / dt : 0.0;
  if (slope > 0.0) {
    end_ms = std::min(end_ms, last.ms + (kFullScale - last.out) / slope);
  } else if (slope < 0.0) {
    end_ms = std::min(end_ms, last.ms + (0.0 - last.out) / slope);
  }
  end_out = std::clamp(last.out + slope * (end_ms - last.ms), 0.0, kFullScale);
  return end_ms > last.ms;
}

/**
 * Walks a polyline plus its extrapolated tail as a sequence of edges.
 *
 * `edges` is one past the last real point when there is a tail; edge i runs
 * from point i to point i + 1, or from the last point to the tail.
 */
struct EdgeWalk {
  const std::vector<ym2612_eg::CurvePoint> *points = nullptr;
  const PlotArea *plot = nullptr;
  bool has_tail = false;
  double tail_ms = 0.0;
  double tail_out = 0.0;

  size_t count() const {
    if (points->size() < 2) {
      return points->size() == 1 && has_tail ? 1 : 0;
    }
    return points->size() - 1 + (has_tail ? 1 : 0);
  }

  /// The edge's endpoints in pixels; returns the ms the edge starts at, which
  /// is what decides which parameter owns it.
  double at(size_t i, ImVec2 &a, ImVec2 &b) const {
    const ym2612_eg::CurvePoint &p0 = (*points)[std::min(i, points->size() - 1)];
    a = plot->at(p0.ms, p0.out);
    if (i + 1 < points->size()) {
      const ym2612_eg::CurvePoint &p1 = (*points)[i + 1];
      b = plot->at(p1.ms, p1.out);
    } else {
      b = plot->at(tail_ms, tail_out);
    }
    return p0.ms;
  }
};

EdgeWalk edge_walk(const std::vector<ym2612_eg::CurvePoint> &points,
                   const PlotArea &plot, bool flat) {
  EdgeWalk walk;
  walk.points = &points;
  walk.plot = &plot;
  walk.has_tail =
      extrapolated_tail(points, flat, plot.span_ms, walk.tail_ms, walk.tail_out);
  return walk;
}

/**
 * The release: a translucent area from x = 0 down to the floor.
 *
 * It answers "if the note were let go at full volume, how fast does it fall?",
 * which is a property of RR (and of the SSG-EG key-off rules) alone -- so it is
 * drawn from the left edge rather than hung off a key-off that never happened.
 *
 * The fill is one quad per polyline edge rather than a polygon, because an
 * SSG-EG release is not convex and AddConvexPolyFilled would fold it inside
 * out. Anti-aliased fill is switched off for the run so the quads meet without
 * leaving seams between them.
 */
void draw_release_area(ImDrawList *draw_list, const EnvelopeCurve &curve,
                       const PlotArea &plot, ImU32 color) {
  const auto &points = curve.release.points;
  if (points.size() < 2) {
    return;
  }
  const EdgeWalk walk = edge_walk(points, plot, !curve.release_truncated);
  const ImU32 fill = color_with_alpha(color, kFillAlpha);
  const ImDrawListFlags saved_flags = draw_list->Flags;
  draw_list->Flags &= ~ImDrawListFlags_AntiAliasedFill;
  const size_t edges = walk.count();
  for (size_t i = 0; i < edges; ++i) {
    ImVec2 a;
    ImVec2 b;
    walk.at(i, a, b);
    if (b.x <= a.x) {
      continue;
    }
    draw_list->AddQuadFilled(a, b, ImVec2(b.x, plot.max.y),
                             ImVec2(a.x, plot.max.y), fill);
  }
  draw_list->Flags = saved_flags;
}

/**
 * The held envelope: a line, no fill, with the key never released.
 *
 * Each edge is coloured by the parameter that owns the instant it starts at,
 * so AR, DR and SR light up their own stretch. The tail past the last
 * simulated point belongs to whatever was happening there -- the sustain,
 * unless the patch never got that far.
 */
void draw_held_line(ImDrawList *draw_list, const EnvelopeCurve &curve,
                    const PlotArea &plot, const SegmentBounds &bounds,
                    const ImU32 (&colors)[kSegmentCount]) {
  const auto &points = curve.held.points;
  if (points.empty()) {
    return;
  }
  const EdgeWalk walk = edge_walk(points, plot, curve.held_parked);
  const float thickness = ui::scale::px(1.0f);
  const size_t edges = walk.count();
  for (size_t i = 0; i < edges; ++i) {
    ImVec2 a;
    ImVec2 b;
    const double ms = walk.at(i, a, b);
    draw_list->AddLine(a, b, colors[bounds.index_at(ms)], thickness);
  }
}

/// TL and SL have no segment of their own: they are levels, so they light up
/// as a rule across the graph at the level they set.
void draw_level_markers(ImDrawList *draw_list, const EnvelopeCurve &curve,
                        const UIState::EnvelopeState &state,
                        const PlotArea &plot) {
  const float thickness = ui::scale::px(1.0f);
  const auto rule = [&](double out, ImU32 color) {
    const float y = plot.y_of(out);
    draw_list->AddLine(ImVec2(plot.min.x, y), ImVec2(plot.max.x, y), color,
                       thickness);
  };

  if (state.total_level != UIState::EnvelopeState::SliderState::None) {
    rule(curve.peak_out, color_from_slider_state(state.total_level));
  }
  if (state.sustain_level != UIState::EnvelopeState::SliderState::None) {
    rule(curve.sustain_out, color_from_slider_state(state.sustain_level));
  }
}

/// The single warning, bottom left. Wrapped rather than clipped: the longest
/// of them is a little wider than the graph at the smallest UI scale, and a
/// sentence cut off mid-word reads as a bug.
void draw_warning(ImDrawList *draw_list, const char *warning,
                  const PlotArea &plot) {
  if (warning == nullptr) {
    return;
  }
  const float inset = ui::scale::px(3.0f);
  const float wrap_width = plot.width() - inset * 2.0f;
  const ImVec2 size = ImGui::CalcTextSize(warning, nullptr, false, wrap_width);
  const ImVec2 pos(plot.min.x + inset, plot.max.y - size.y - inset);
  draw_list->AddText(
      ImGui::GetFont(), ImGui::GetFontSize(), pos,
      color_with_alpha(ImGui::GetColorU32(ImGuiCol_Text), kWarningAlpha),
      warning, nullptr, wrap_width);
}

} // namespace

void render_envelope_image(const ym2612::OperatorSettings &op,
                           const UIState::EnvelopeState &state, ImVec2 size) {
  // Before BeginChild, so the ID comes from the operator's stack rather than
  // from the child window.
  const EnvelopeCurve &curve =
      cache_for(ImGui::GetID("##envelope_curve")).get(op);

  ImGui::BeginChild("EnvelopeImage", size, false, ImGuiWindowFlags_NoScrollbar);

  ImDrawList *draw_list = ImGui::GetWindowDrawList();

  const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
  const ImVec2 canvas_max(canvas_min.x + size.x, canvas_min.y + size.y);

  draw_list->AddRect(canvas_min, canvas_max,
                     ImGui::GetColorU32(ImGuiCol_Separator));

  // The time labels get a strip of their own along the top: a curve at full
  // volume runs along the very top of the plot, and text over it would be
  // unreadable in both directions.
  const float label_height = ImGui::GetTextLineHeight();
  PlotArea plot;
  plot.min = ImVec2(canvas_min.x + 1.0f, canvas_min.y + label_height + 1.0f);
  plot.max = ImVec2(canvas_max.x - 1.0f, canvas_max.y - 1.0f);
  plot.span_ms = std::max(curve.span_ms, 1.0);

  draw_time_grid(draw_list, plot, canvas_min.y + 1.0f);

  const ImU32 colors[kSegmentCount] = {
      color_from_slider_state(state.attack_rate),
      color_from_slider_state(state.decay_rate),
      color_from_slider_state(state.sustain_rate),
      color_from_slider_state(state.release_rate),
  };
  draw_release_area(draw_list, curve, plot, colors[kRelease]);
  draw_held_line(draw_list, curve, plot, segment_bounds(curve), colors);
  draw_level_markers(draw_list, curve, state, plot);
  draw_warning(draw_list, curve.warning, plot);

  ImGui::EndChild();
}

} // namespace ui
