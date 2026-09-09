#include "envelope_image.hpp"

#include "app_state.hpp"
#include "common.hpp"
#include "gui/envelope/envelope_curve.hpp"
#include "gui/ui_scale.hpp"
#include "ym2612/note.hpp"

#include <array>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <imgui.h>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * The envelope graph: the held envelope (attack, decay, sustain, key never
 * released) as a line, the release from full volume at x = 0 as a translucent
 * filled area, and each sounding voice as a faint curve plus a thin vertical
 * cursor at where it has got to. The shapes, the milliseconds across and the
 * attenuation down all come from ym2612_eg by way of gui/envelope; this file
 * only turns them into pixels.
 */

namespace ui {
namespace {

using ui::envelope::EnvelopeCurve;
using ui::envelope::EnvelopeCurveCache;
using ui::envelope::VoiceCurveCache;

/// Attenuation at the bottom of the graph; 0 (full volume) is at the top.
constexpr double kFullScale = static_cast<double>(ym2612_eg::kMaxAttenuation);

/// The wash under the release.
constexpr float kFillAlpha = 0.30f;
/// The warning line is a footnote, not an alert.
constexpr float kWarningAlpha = 0.6f;

/// How a sounding voice is drawn: the ghost curve well below the reference
/// curve's weight, the cursor well above it, each older voice in a chord
/// fainter than the last, and a finished voice fading out over kVoiceFadeMs.
constexpr float kVoiceCurveAlpha = 0.30f;
constexpr float kVoiceCursorAlpha = 0.85f;
constexpr float kVoiceRecencyFalloff = 0.65f;
constexpr double kVoiceFadeMs = 400.0;
/// Below this a voice is not worth the draw calls.
constexpr float kVoiceMinAlpha = 0.02f;

/// How many voice curves the whole editor may simulate in one frame. Building
/// one is expensive enough that four at once would stutter, so they queue: a
/// voice without a curve yet is not drawn, and arrives a frame or two later.
constexpr int kVoiceBuildsPerFrame = 1;

/// The budget above, refilled once per ImGui frame and shared by all four
/// operators.
int &voice_build_budget() {
  static int frame = -1;
  static int budget = 0;
  const int now = ImGui::GetFrameCount();
  if (now != frame) {
    frame = now;
    budget = kVoiceBuildsPerFrame;
  }
  return budget;
}

/// Which parameter owns a stretch of the curve. The boundaries are the curve's
/// own markers rather than anything re-derived from the registers.
enum SegmentIndex {
  kAttack = 0,
  kDecay = 1,
  kSustain = 2,
  kRelease = 3,
  kSegmentCount = 4,
};

/// Time constant, in seconds, of the axis' exponential approach to a new width.
constexpr float kAxisTimeConstantSec = 0.12f;

/// Everything one operator's graph remembers between frames: the curve, and
/// the width the axis was last drawn at. operator_editor pushes the slot onto
/// the ID stack, so the map never holds more than four entries.
struct EnvelopeSlot {
  EnvelopeCurveCache curve;
  /// The curves of whatever is sounding, keyed on key-scale value rather than
  /// on the note; see VoiceCurveCache.
  VoiceCurveCache voices;
  /// 0 until this operator has been drawn once; the first frame then starts at
  /// its target instead of growing into it from nothing.
  double drawn_span_ms = 0.0;
  /// The voices this operator has already watched fade out. A released voice
  /// only ever gets quieter, so one that has gone is never drawn again. Six
  /// voices can sound, so six sequence numbers are enough.
  std::array<uint64_t, VoiceCurveCache::kMaxEntries> finished{};
};

bool already_finished(const EnvelopeSlot &slot, uint64_t sequence) {
  return sequence != 0 &&
         std::find(slot.finished.begin(), slot.finished.end(), sequence) !=
             slot.finished.end();
}

void remember_finished(EnvelopeSlot &slot, uint64_t sequence) {
  if (sequence == 0) {
    return;
  }
  // Sequences only grow, so the smallest entry is the one furthest in the past
  // and the safest to forget.
  *std::min_element(slot.finished.begin(), slot.finished.end()) = sequence;
}

/// True while one of this operator's envelope sliders is being dragged.
bool envelope_slider_active(const UIState::EnvelopeState &state) {
  using Slider = UIState::EnvelopeState::SliderState;
  return state.total_level == Slider::Active ||
         state.attack_rate == Slider::Active ||
         state.decay_rate == Slider::Active ||
         state.sustain_level == Slider::Active ||
         state.sustain_rate == Slider::Active ||
         state.release_rate == Slider::Active;
}

EnvelopeSlot &slot_for(ImGuiID id) {
  static std::unordered_map<ImGuiID, EnvelopeSlot> slots;
  return slots[id];
}

/// One frame of the axis' approach to `target_ms`. `snap_ms` is a pixel's
/// worth of width: below that the remaining motion cannot be seen.
double approach_span(double current_ms, double target_ms, float dt_sec,
                     double snap_ms) {
  if (!(current_ms > 0.0)) {
    return target_ms; // first frame: start where we are going
  }
  if (std::fabs(target_ms - current_ms) <= snap_ms || !(dt_sec > 0.0f)) {
    return target_ms;
  }
  // Exponential in real time, so the motion is the same whatever the frame
  // rate, and a stalled frame lands most of the way there.
  const double alpha =
      1.0 - std::exp(-static_cast<double>(dt_sec) / kAxisTimeConstantSec);
  return current_ms + (target_ms - current_ms) * alpha;
}

/// Hovering a slider and dragging it light the same stretch the same way.
ImU32 color_from_slider_state(
    const UIState::EnvelopeState::SliderState &state) {
  return state == UIState::EnvelopeState::SliderState::None
             ? ImGui::GetColorU32(ImGuiCol_Text)
             : ImGui::GetColorU32(ImGuiCol_FrameBgActive);
}

/// The instants the held line changes hands. A marker is negative when the
/// segment never happened, and the segment before it runs on, so each boundary
/// is pinned to the one before. No key-off here: the sustain owns everything
/// past the decay.
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
  /// The width being drawn this frame, which during an animation is somewhere
  /// between the last one and the target.
  double span_ms = 1.0;
  /// The width it is heading for.
  double target_ms = 1.0;

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

/// The one subdued colour for every caption along the top strip.
ImU32 axis_label_color() {
  return color_with_alpha(ImGui::GetColorU32(ImGuiCol_Text), kWarningAlpha);
}

/**
 * The time grid, labelled every nth line -- n being however many it takes for
 * the widest label to fit between two of them, so the labels stay evenly
 * spaced. Every line is drawn. `label_limit_x` is where the text has to stop:
 * the right edge of the plot, less whatever the note label there has claimed.
 */
void draw_time_grid(ImDrawList *draw_list, const PlotArea &plot,
                    float label_baseline, float label_limit_x) {
  const ImU32 grid_color = ImGui::GetColorU32(ImGuiCol_Separator);
  const ImU32 label_color = axis_label_color();
  // The width being drawn, not the one being animated towards: the lines have
  // to cover what is actually on screen.
  const double drawn_ms = std::max(plot.span_ms, 1.0);
  const double step = ui::envelope::grid_step_ms(drawn_ms);
  const int lines = static_cast<int>(drawn_ms / step + 1e-6);

  char widest[16];
  format_ms(widest, step * static_cast<int>(drawn_ms / step + 1e-6));
  const float needed = ImGui::CalcTextSize(widest).x + ui::scale::px(6.0f);
  const float pitch = plot.width() * static_cast<float>(step / drawn_ms);
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
    if (x + ImGui::CalcTextSize(label).x > label_limit_x) {
      continue; // would hang off the right edge, or run into the note
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
 * One trace turned into the polyline that is actually drawn: entered at
 * `from_ms`, stopped at `limit_ms`, and slid along the axis by `shift_ms`. The
 * edge straddling either end is cut there because x_of() clamps, so an uncut
 * edge would smear down the last column. The path is continued past the
 * trace's last point along `slope` -- the curve's own -- so a line never stops
 * in mid-air; zero continues it flat.
 */
struct TracePath {
  /// The polyline in pixels.
  std::vector<ImVec2> pixels;
  /// Where on the trace each vertex sits, so a drawer can ask which parameter
  /// owns the edge starting there. Same length as `pixels`.
  std::vector<double> at_ms;

  size_t edges() const { return pixels.size() < 2 ? 0 : pixels.size() - 1; }
};

void build_trace_path(TracePath &path,
                      const std::vector<ym2612_eg::CurvePoint> &points,
                      const PlotArea &plot, double slope, double from_ms,
                      double limit_ms, double shift_ms) {
  path.pixels.clear();
  path.at_ms.clear();
  if (points.empty()) {
    return;
  }
  // Everything below is in the trace's own time; the shift is applied once, on
  // the way to pixels.
  const double limit = std::min(limit_ms, plot.span_ms) - shift_ms;
  if (!(limit > from_ms)) {
    return;
  }

  // The tail: one more piece of curve past the last simulated point, cut short
  // if the slope would carry it off the top or the bottom of the plot.
  const ym2612_eg::CurvePoint &last = points.back();
  double tail_ms = limit;
  double tail_out = last.out;
  bool has_tail = last.ms < limit;
  if (has_tail && slope > 0.0) {
    tail_ms = std::min(tail_ms, last.ms + (kFullScale - last.out) / slope);
  } else if (has_tail && slope < 0.0) {
    tail_ms = std::min(tail_ms, last.ms + (0.0 - last.out) / slope);
  }
  if (has_tail) {
    tail_out =
        std::clamp(last.out + slope * (tail_ms - last.ms), 0.0, kFullScale);
    has_tail = tail_ms > last.ms;
  }

  const size_t edges = points.size() - 1 + (has_tail ? 1 : 0);
  path.pixels.reserve(edges + 1);
  path.at_ms.reserve(edges + 1);

  for (size_t i = 0; i < edges; ++i) {
    double ms0 = points[std::min(i, points.size() - 1)].ms;
    double out0 = points[std::min(i, points.size() - 1)].out;
    double ms1 = tail_ms;
    double out1 = tail_out;
    if (i + 1 < points.size()) {
      ms1 = points[i + 1].ms;
      out1 = points[i + 1].out;
    }
    if (ms1 <= from_ms) {
      continue; // still before the point the trace is entered at
    }
    if (ms0 >= limit) {
      break; // past the end of what may be drawn
    }
    if (ms0 < from_ms) {
      // Start exactly where the trace is entered, between the two straddling
      // points, rather than at whichever vertex happens to follow.
      const double dt = ms1 - ms0;
      const double t = dt > 0.0 ? (from_ms - ms0) / dt : 0.0;
      out0 = out0 + (out1 - out0) * t;
      ms0 = from_ms;
    }
    if (ms1 > limit) {
      const double dt = ms1 - ms0;
      const double t = dt > 0.0 ? (limit - ms0) / dt : 0.0;
      out1 = out0 + (out1 - out0) * t;
      ms1 = limit;
    }
    if (path.pixels.empty()) {
      path.pixels.push_back(plot.at(ms0 + shift_ms, out0));
      path.at_ms.push_back(ms0);
    }
    path.pixels.push_back(plot.at(ms1 + shift_ms, out1));
    path.at_ms.push_back(ms1);
    if (ms1 >= limit) {
      break;
    }
  }
}

/// The scratch the paths are built into. One graph is drawn at a time, so a
/// single buffer serves every drawer and allocates once for the process.
TracePath &trace_scratch() {
  static TracePath path;
  return path;
}

/**
 * The release: a translucent area from x = 0 down to the floor, since what it
 * answers is a property of RR and the SSG-EG key-off rules alone. The fill is
 * one quad per polyline edge rather than a polygon, because an SSG-EG release
 * is not convex and AddConvexPolyFilled would fold it inside out;
 * anti-aliased fill is off for the run so the quads meet without seams.
 */
void draw_release_area(ImDrawList *draw_list, const EnvelopeCurve &curve,
                       const PlotArea &plot, ImU32 color) {
  const auto &points = curve.release.points;
  if (points.size() < 2) {
    return;
  }
  TracePath &path = trace_scratch();
  build_trace_path(path, points, plot, curve.release_tail_slope, 0.0,
                   plot.span_ms, 0.0);
  const ImU32 fill = color_with_alpha(color, kFillAlpha);
  const ImDrawListFlags saved_flags = draw_list->Flags;
  draw_list->Flags &= ~ImDrawListFlags_AntiAliasedFill;
  const size_t edges = path.edges();
  for (size_t i = 0; i < edges; ++i) {
    const ImVec2 &a = path.pixels[i];
    const ImVec2 &b = path.pixels[i + 1];
    if (b.x <= a.x) {
      continue;
    }
    draw_list->AddQuadFilled(a, b, ImVec2(b.x, plot.max.y),
                             ImVec2(a.x, plot.max.y), fill);
  }
  draw_list->Flags = saved_flags;
}

/// The held envelope: a line, no fill, with the key never released. Each edge
/// is coloured by the parameter that owns the instant it starts at; the tail
/// past the last simulated point belongs to whatever was happening there.
void draw_held_line(ImDrawList *draw_list, const EnvelopeCurve &curve,
                    const PlotArea &plot, const SegmentBounds &bounds,
                    const ImU32 (&colors)[kSegmentCount]) {
  const auto &points = curve.held.points;
  if (points.empty()) {
    return;
  }
  TracePath &path = trace_scratch();
  build_trace_path(path, points, plot, curve.held_tail_slope, 0.0, plot.span_ms,
                   0.0);
  const float thickness = ui::scale::px(1.0f);
  const size_t edges = path.edges();
  // One polyline per stretch of one colour: the boundaries are the segment
  // markers, so there are three runs at most however many thousand vertices an
  // SSG trace carries.
  size_t run_start = 0;
  while (run_start < edges) {
    const int owner = bounds.index_at(path.at_ms[run_start]);
    size_t run_end = run_start + 1;
    while (run_end < edges && bounds.index_at(path.at_ms[run_end]) == owner) {
      ++run_end;
    }
    draw_list->AddPolyline(&path.pixels[run_start],
                           static_cast<int>(run_end - run_start + 1),
                           colors[owner], ImDrawFlags_None, thickness);
    run_start = run_end;
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

/// One voice's cursor: a thin vertical rule at where that note has actually
/// got to on its own envelope.
void draw_voice_cursor(ImDrawList *draw_list, const PlotArea &plot, double ms,
                       ImU32 color) {
  // A voice past the end of the axis leaves the graph rather than parking on
  // its edge, where it would read as "the envelope stopped here".
  if (ms < 0.0 || ms > plot.span_ms) {
    return;
  }
  const float x = plot.x_of(ms);
  draw_list->AddLine(ImVec2(x, plot.min.y), ImVec2(x, plot.max.y), color,
                     ui::scale::px(1.0f));
}

/// The voice's own attack, decay and sustain, in one flat colour at a fraction
/// of the reference curve's weight. No release: the release this voice took is
/// drawn separately, from where the key actually came up.
void draw_voice_curve(ImDrawList *draw_list, const EnvelopeCurve &curve,
                      const PlotArea &plot, double to_ms, ImU32 color) {
  const std::vector<ym2612_eg::CurvePoint> &points = curve.held.points;
  if (points.size() < 2) {
    return;
  }
  TracePath &path = trace_scratch();
  build_trace_path(path, points, plot, curve.held_tail_slope, 0.0, to_ms, 0.0);
  if (path.pixels.size() < 2) {
    return;
  }
  draw_list->AddPolyline(path.pixels.data(),
                         static_cast<int>(path.pixels.size()), color,
                         ImDrawFlags_None, ui::scale::px(1.0f));
}

/// The release this voice is actually taking: the drawn release trace entered
/// at the level the key came up on. A line, like the voice's own attack and
/// decay, so it reads as this voice rather than as the reference.
void draw_voice_release_line(ImDrawList *draw_list, const EnvelopeCurve &curve,
                             const PlotArea &plot, double from_ms,
                             double origin_ms, double to_ms, ImU32 color) {
  const std::vector<ym2612_eg::CurvePoint> &points = curve.release.points;
  if (from_ms < 0.0 || origin_ms < 0.0 || points.size() < 2) {
    return;
  }
  TracePath &path = trace_scratch();
  // The release keeps its shape and is slid along to where the key came up.
  build_trace_path(path, points, plot, curve.release_tail_slope, from_ms, to_ms,
                   origin_ms - from_ms);
  if (path.pixels.size() < 2) {
    return;
  }
  draw_list->AddPolyline(path.pixels.data(),
                         static_cast<int>(path.pixels.size()), color,
                         ImDrawFlags_None, ui::scale::px(1.0f));
}

/// The single warning, bottom left. Wrapped rather than clipped: it is a
/// little wider than the graph at the smallest UI scale.
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

EnvelopeVoices collect_envelope_voices(const VoiceActivityFrame &frame) {
  EnvelopeVoices out;
  const double rate =
      frame.sample_rate > 0 ? static_cast<double>(frame.sample_rate) : 44100.0;
  const double ms_per_sample = 1000.0 / rate;
  // Past this a released voice has nothing left on any graph: the release is
  // only ever simulated this far, and the fade is over well before then.
  const double keep_ms = ui::envelope::release_max_ms() + kVoiceFadeMs;

  // `now` is never behind a stamp, but saturating keeps the arithmetic safe
  // across an engine restart, which puts the clock back to zero.
  const auto elapsed_ms = [&](uint64_t from) {
    return frame.now_samples > from
               ? static_cast<double>(frame.now_samples - from) * ms_per_sample
               : 0.0;
  };

  // Newest first: `sequence` counts key-ons across the whole allocator, so it
  // is the recency order, and a stolen channel arrives as a strictly greater
  // sequence.
  std::array<const VoiceActivity *, 6> ordered{};
  int found = 0;
  for (const VoiceActivity &voice : frame.voices) {
    if (voice.valid()) {
      ordered[found++] = &voice;
    }
  }
  std::sort(ordered.begin(), ordered.begin() + found,
            [](const VoiceActivity *a, const VoiceActivity *b) {
              return a->sequence > b->sequence;
            });

  float recency = 1.0f;
  for (int i = 0; i < found; ++i) {
    const VoiceActivity &voice = *ordered[i];
    EnvelopeVoices::Voice item;
    item.sequence = voice.sequence;
    item.midi_note = voice.midi_note;
    item.since_key_on_ms = elapsed_ms(voice.key_on_sample);
    item.since_key_off_ms =
        voice.held ? -1.0 : elapsed_ms(voice.key_off_sample);
    if (item.since_key_off_ms > keep_ms) {
      continue;
    }
    item.recency = recency;
    recency *= kVoiceRecencyFalloff;
    out.items[out.count++] = item;
  }
  return out;
}

void render_envelope_image(const ym2612::OperatorSettings &op,
                           const UIState::EnvelopeState &state, ImVec2 size,
                           const EnvelopeVoices &voices) {
  // Before BeginChild, so the ID comes from the operator's stack rather than
  // from the child window.
  EnvelopeSlot &slot = slot_for(ImGui::GetID("##envelope_curve"));
  const EnvelopeCurve &curve = slot.curve.get(op);

  PlotArea plot;
  plot.target_ms = std::max(curve.span_ms, 1.0);

  // The axis follows the target rather than jumping to it. The curve is
  // already in milliseconds, so this is purely a change of scale at draw time
  // and nothing is recomputed. The snap threshold is target/width, which is
  // the difference that moves the right-hand end of the content by one pixel.
  // Kept outside the visibility test below, so a graph scrolled back into view
  // is where it would have been rather than starting the journey again.
  const float plot_width = std::max(size.x - 2.0f, 1.0f);
  slot.drawn_span_ms =
      approach_span(slot.drawn_span_ms, plot.target_ms, ImGui::GetIO().DeltaTime,
                    plot.target_ms / plot_width);
  plot.span_ms = std::max(slot.drawn_span_ms, 1.0);

  // BeginChild answers whether anything inside it can be seen; without the
  // test an off-screen graph still builds its whole vertex stream for the
  // clipper to throw away.
  const bool visible =
      ImGui::BeginChild("EnvelopeImage", size, false, ImGuiWindowFlags_NoScrollbar);
  if (!visible) {
    ImGui::EndChild();
    return;
  }

  ImDrawList *draw_list = ImGui::GetWindowDrawList();

  const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
  const ImVec2 canvas_max(canvas_min.x + size.x, canvas_min.y + size.y);

  draw_list->AddRect(canvas_min, canvas_max,
                     ImGui::GetColorU32(ImGuiCol_Separator));

  // The time labels get a strip of their own along the top: a curve at full
  // volume runs along the very top of the plot.
  const float label_height = ImGui::GetTextLineHeight();
  plot.min = ImVec2(canvas_min.x + 1.0f, canvas_min.y + label_height + 1.0f);
  plot.max = ImVec2(canvas_max.x - 1.0f, canvas_max.y - 1.0f);

  // The note the axis is drawn at, at the far end of the same strip; the
  // milliseconds stop short of it.
  const float label_baseline = canvas_min.y + 1.0f;
  const std::string note_name =
      ym2612::Note::from_midi_note(
          static_cast<uint8_t>(ui::envelope::reference_midi_note()))
          .name();
  const float note_x = plot.max.x - ImGui::CalcTextSize(note_name.c_str()).x;
  draw_list->AddText(ImVec2(note_x, label_baseline), axis_label_color(),
                     note_name.c_str());

  draw_time_grid(draw_list, plot, label_baseline, note_x - ui::scale::px(6.0f));

  const ImU32 colors[kSegmentCount] = {
      color_from_slider_state(state.attack_rate),
      color_from_slider_state(state.decay_rate),
      color_from_slider_state(state.sustain_rate),
      color_from_slider_state(state.release_rate),
  };
  // What is sounding, newest first, which is also the order the frame's one
  // curve build is offered in.
  struct DrawnVoice {
    const EnvelopeCurve *curve;
    ui::envelope::VoiceCursor cursor;
    float alpha;
  };
  // While a slider is being dragged the registers move under the cache every
  // frame, so a curve simulated now is dropped before it is ever drawn twice.
  int nothing_to_spend = 0;
  int &build_budget = envelope_slider_active(state) ? nothing_to_spend
                                                    : voice_build_budget();

  DrawnVoice drawn[6];
  int drawn_count = 0;
  for (int i = 0; i < voices.count; ++i) {
    const EnvelopeVoices::Voice &voice = voices.items[i];
    if (already_finished(slot, voice.sequence)) {
      continue; // this note is over on this operator
    }
    const EnvelopeCurve *voice_curve =
        slot.voices.get(op, ym2612_eg::NotePitch::from_midi(voice.midi_note),
                        curve, build_budget);
    if (voice_curve == nullptr) {
      continue; // its curve is being built next frame
    }
    const ui::envelope::VoiceCursor cursor =
        ui::envelope::cursor_for_voice(*voice_curve, voice.since_key_on_ms,
                                       voice.since_key_off_ms, plot.span_ms);
    // A voice that has gone quiet fades out rather than vanishing on the
    // frame its release ends.
    const float fade = static_cast<float>(
        std::clamp(1.0 - cursor.silent_for_ms / kVoiceFadeMs, 0.0, 1.0));
    const float alpha = voice.recency * fade;
    if (alpha < kVoiceMinAlpha) {
      // Faded out for good rather than merely quiet: the silence a released
      // voice sits in only ever gets older, so nothing can bring it back.
      if (cursor.released && cursor.silent_for_ms >= kVoiceFadeMs) {
        remember_finished(slot, voice.sequence);
      }
      continue;
    }
    drawn[drawn_count++] = DrawnVoice{voice_curve, cursor, alpha};
  }

  // Ghost curves oldest first, so the newest is the one on top of the others
  // -- and all of them under the curve being edited.
  const ImU32 ghost_base = ImGui::GetColorU32(ImGuiCol_Text);
  for (int i = drawn_count - 1; i >= 0; --i) {
    // Nothing to draw when the note shares the reference note's key-scale
    // value: its curve IS the one already on screen.
    if (drawn[i].curve == &curve) {
      continue;
    }
    draw_voice_curve(
        draw_list, *drawn[i].curve, plot, drawn[i].cursor.held_to_ms,
        color_with_alpha(ghost_base, drawn[i].alpha * kVoiceCurveAlpha));
  }
  // The release each voice is taking, over the ghosts and under the reference
  // curve. Drawn whatever its key scale: it is the one part of a note the
  // reference curve cannot stand in for.
  for (int i = drawn_count - 1; i >= 0; --i) {
    draw_voice_release_line(
        draw_list, *drawn[i].curve, plot, drawn[i].cursor.release_from_ms,
        drawn[i].cursor.release_origin_ms, drawn[i].cursor.ms,
        color_with_alpha(ghost_base, drawn[i].alpha * kVoiceCurveAlpha));
  }

  draw_release_area(draw_list, curve, plot, colors[kRelease]);
  draw_held_line(draw_list, curve, plot, segment_bounds(curve), colors);
  draw_level_markers(draw_list, curve, state, plot);
  // Over everything, so a cursor is never hidden under the curve it measures.
  const ImU32 cursor_base = ImGui::GetColorU32(ImGuiCol_FrameBgActive);
  for (int i = drawn_count - 1; i >= 0; --i) {
    draw_voice_cursor(
        draw_list, plot, drawn[i].cursor.ms,
        color_with_alpha(cursor_base, drawn[i].alpha * kVoiceCursorAlpha));
  }
  draw_warning(draw_list, curve.warning, plot);

  ImGui::EndChild();
}

} // namespace ui
