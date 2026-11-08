#include "patch_editor_tools.hpp"

#include "formats/ym2612_format_adapter.hpp"
#include "gui/clipboard.hpp"
#include "gui/components/named_slider.hpp"
#include "gui/components/section_header.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include "patches/patch_tweak.hpp"
#include "ym2612/operator_edit.hpp"
#include "ym2612/playback.hpp"
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <utility>

namespace ui {
namespace {

using SliderState = UIState::EnvelopeState::SliderState;

/// Lights on each operator the fields `tweak` moves, as its own slider would.
void light_tweak_fields(PatchEditorContext &context,
                        const ym2612::ChannelInstrument &instrument,
                        patch_lab::Tweak tweak, SliderState lit) {
  const auto fields = patch_lab::tweak_fields(instrument, tweak);
  for (int slot = 0; slot < 4; ++slot) {
    auto &quick = context.envelope_state(slot).quick_edit;
    // The fields come in register order; the editor's slots are OP1..OP4.
    const auto &moved =
        fields[static_cast<std::size_t>(ym2612::operator_register_index(slot))];
    const auto raise = [lit](SliderState &field, bool on) {
      if (on) {
        field = UIState::EnvelopeState::stronger(field, lit);
      }
    };
    raise(quick.total_level, moved.total_level);
    raise(quick.attack_rate, moved.attack_rate);
    raise(quick.decay_rate, moved.decay_rate);
    raise(quick.sustain_level, moved.sustain_level);
    raise(quick.sustain_rate, moved.sustain_rate);
    raise(quick.release_rate, moved.release_rate);
  }
}

/// Draws one slider; returns how it is pointed at.
SliderState render_quick_edit_slider(PatchEditorContext &context,
                                     ym2612::ChannelInstrument &instrument,
                                     const char *label,
                                     patch_lab::Tweak tweak) {
  auto &base = context.document_ui(context.document).quick_edit_base;
  const auto value = patch_lab::tweak_value(instrument, tweak);
  const auto range = patch_lab::tweak_range(base.value_or(instrument), tweak);
  // While dragged, how far the drag has gone; otherwise the register.
  const auto text = base && ImGui::GetActiveID() == named_slider_id(label)
                        ? patch_lab::tweak_change_text(*base, instrument, tweak)
                        : patch_lab::tweak_text(instrument, tweak);

  // Rates run like the operator editor's sliders: toward the end that
  // sounds longer.
  const bool longer_first = tweak == patch_lab::Tweak::Attack ||
                            tweak == patch_lab::Tweak::Decay ||
                            tweak == patch_lab::Tweak::Release;
  int shown = value.value_or(range.min);
  ImGui::BeginDisabled(!value);
  // No typing: a slider that moves several registers has no one number to
  // type.
  const bool changed =
      named_slider_int(label, &shown, longer_first ? range.max : range.min,
                       longer_first ? range.min : range.max, text.c_str(),
                       ImGuiSliderFlags_NoInput);
  ImGui::EndDisabled();

  const SliderState lit = ImGui::IsItemActive()    ? SliderState::Active
                          : ImGui::IsItemHovered() ? SliderState::Hover
                                                   : SliderState::None;
  if (lit != SliderState::None) {
    light_tweak_fields(context, instrument, tweak, lit);
  }

  if (ImGui::IsItemActivated()) {
    base = instrument;
    // Empty merge key: each drag is its own undo step.
    context.begin_history(std::string("Quick Edit ") + label, {});
  }
  if (changed && base) {
    instrument = patch_lab::with_tweak(*base, tweak, shown);
  }
  if (ImGui::IsItemDeactivated()) {
    base.reset();
  }
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    context.commit_history();
  }
  return lit;
}

constexpr int kTweakCount = 5;
/// The sketch's handles' radius, before the interface's scale.
constexpr float kSketchHandleRadius = 6.0f;

/// Where the sketch draws the envelope's corners, from its rough shape.
struct SketchPoints {
  float left, right, top, floor;
  ImVec2 peak;     ///< The end of the attack.
  ImVec2 knee;     ///< The end of the decay.
  ImVec2 held_end; ///< The held line at the right.
  ImVec2 release;  ///< Where the release reaches the floor.
  /// How far each part reaches across, or rises, from none to all of it.
  float attack_span, decay_span, level_span, release_span;
};

SketchPoints sketch_points(const patch_lab::TweakShape &shape,
                           const ImVec2 &min, const ImVec2 &size) {
  SketchPoints p;
  const float pad = ui::scale::px(4.0f);
  p.left = min.x + pad;
  p.right = min.x + size.x - pad;
  p.top = min.y + pad;
  p.floor = min.y + size.y - pad;
  const float width = p.right - p.left;
  const float height = p.floor - p.top;
  p.attack_span = width * 0.22f;
  p.decay_span = width * 0.28f;
  p.level_span = height * 0.85f;
  p.release_span = width * 0.9f;
  // Never flat on the floor, so a quiet patch still has a shape to show.
  const float peak_y = p.floor - height * 0.15f - p.level_span * shape.level;
  const float attack_end =
      p.left + width * 0.02f + p.attack_span * shape.attack;
  const float decay_end =
      attack_end + width * 0.03f + p.decay_span * shape.decay;
  const float sustain = p.floor - (p.floor - peak_y) * shape.sustain;
  p.peak = ImVec2(attack_end, peak_y);
  p.knee = ImVec2(decay_end, sustain);
  p.held_end = ImVec2(p.right, sustain + (p.floor - sustain) * shape.hold);
  p.release = ImVec2(std::min(p.right, p.left + width * 0.04f +
                                           p.release_span * shape.release),
                     p.floor);
  return p;
}

/// The sketch's handles: the peak (attack across, volume up), the knee
/// (decay) and the release's end (release).
enum class SketchHandle { None, Peak, Knee, Release };

/// The sketch's handle under the pointer or held, and whether the sketch is
/// pointed at at all, which shows its handles.
struct SketchPointer {
  SketchHandle handle = SketchHandle::None;
  bool held = false;
  bool over = false;
};

/// What a sketch handle moves, lit while it is pointed at.
bool handle_moves(SketchHandle handle, patch_lab::Tweak tweak) {
  switch (handle) {
  case SketchHandle::Peak:
    return tweak == patch_lab::Tweak::Attack ||
           tweak == patch_lab::Tweak::Volume;
  case SketchHandle::Knee:
    return tweak == patch_lab::Tweak::Decay;
  case SketchHandle::Release:
    return tweak == patch_lab::Tweak::Release;
  case SketchHandle::None:
    break;
  }
  return false;
}

/**
 * A sketch of the envelope the sliders move, drawn the way an operator's
 * graph is: the held line, and the release as an area from the start. Not
 * to scale; it shows which way each slider takes the shape. Its corners are
 * handles too, dragged as the operator graph's are.
 */
void draw_quick_edit_sketch(const ym2612::ChannelInstrument &instrument,
                            const SliderState (&lit)[kTweakCount],
                            const SketchPointer &pointer, const ImVec2 &min,
                            const ImVec2 &size) {
  const ImVec2 max(min.x + size.x, min.y + size.y);
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  draw_list->AddRectFilled(min, max,
                           styles::color_u32(styles::MegatoyCol::EnvelopeBg));
  draw_list->AddRect(min, max,
                     styles::color_u32(styles::MegatoyCol::EnvelopeGrid));
  const auto shape = patch_lab::tweak_shape(instrument);
  if (!shape) {
    return;
  }
  const SketchPoints p = sketch_points(*shape, min, size);

  const auto is_lit = [&lit](patch_lab::Tweak tweak) {
    return lit[static_cast<int>(tweak)] != SliderState::None;
  };
  const auto curve_color = [&](patch_lab::Tweak tweak) {
    return styles::color_u32(is_lit(tweak) || is_lit(patch_lab::Tweak::Volume)
                                 ? styles::MegatoyCol::EnvelopeCurveActive
                                 : styles::MegatoyCol::EnvelopeCurve);
  };

  draw_list->AddTriangleFilled(
      ImVec2(p.left, p.peak.y), p.release, ImVec2(p.left, p.floor),
      styles::color_u32(is_lit(patch_lab::Tweak::Release)
                            ? styles::MegatoyCol::EnvelopeReleaseActive
                            : styles::MegatoyCol::EnvelopeRelease));

  const float thickness = ui::scale::px(1.5f);
  draw_list->AddLine(ImVec2(p.left, p.floor), p.peak,
                     curve_color(patch_lab::Tweak::Attack), thickness);
  draw_list->AddLine(p.peak, p.knee, curve_color(patch_lab::Tweak::Decay),
                     thickness);
  draw_list->AddLine(p.knee, p.held_end, curve_color(patch_lab::Tweak::Volume),
                     thickness);

  // The handles, while the sketch is pointed at, as the operator graph's.
  if (!pointer.over && !pointer.held) {
    return;
  }
  const float radius = ui::scale::px(kSketchHandleRadius);
  for (const auto &[handle, at] :
       {std::pair{SketchHandle::Peak, p.peak},
        std::pair{SketchHandle::Knee, p.knee},
        std::pair{SketchHandle::Release, p.release}}) {
    draw_list->AddCircleFilled(
        at, radius,
        styles::color_u32(handle == pointer.handle
                              ? styles::MegatoyCol::EnvelopeHandleActive
                              : styles::MegatoyCol::EnvelopeHandle));
  }
}

/**
 * The sketch as something to drag: a press near a handle takes it, and the
 * pointer's travel from the press moves what it stands for, from the
 * instrument as it was, by the sketch's own scale.
 */
SketchPointer drag_quick_edit_sketch(PatchEditorContext &context,
                                     ym2612::ChannelInstrument &instrument,
                                     const ImVec2 &min, const ImVec2 &size) {
  static SketchHandle held = SketchHandle::None;
  auto &base = context.document_ui(context.document).quick_edit_base;
  ImGui::InvisibleButton("##quick_edit_sketch", size);
  SketchPointer pointer;
  pointer.over = ImGui::IsItemHovered();
  const auto shape = patch_lab::tweak_shape(instrument);
  if (!shape) {
    return pointer;
  }

  if (ImGui::IsItemActivated()) {
    held = SketchHandle::None;
    const SketchPoints p = sketch_points(*shape, min, size);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    float best = ui::scale::px(kSketchHandleRadius * 3.0f);
    for (const auto &[handle, at] :
         {std::pair{SketchHandle::Peak, p.peak},
          std::pair{SketchHandle::Knee, p.knee},
          std::pair{SketchHandle::Release, p.release}}) {
      const float distance = std::hypot(mouse.x - at.x, mouse.y - at.y);
      if (distance <= best) {
        best = distance;
        held = handle;
      }
    }
    if (held != SketchHandle::None) {
      base = instrument;
      // Empty merge key: each drag is its own undo step.
      context.begin_history("Quick Edit Envelope", {});
    }
  }

  if (held != SketchHandle::None && ImGui::IsItemActive() && base) {
    const SketchPoints p =
        sketch_points(*patch_lab::tweak_shape(*base), min, size);
    const ImVec2 moved = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
    // A tweak moved by `px` of travel over `span` pixels for its whole range;
    // `sign` turns travel right or down into the value's direction.
    const auto step = [&](ym2612::ChannelInstrument from,
                          patch_lab::Tweak tweak, float px, float span,
                          float sign) {
      const auto value = patch_lab::tweak_value(*base, tweak);
      if (!value || span <= 0.0f) {
        return from;
      }
      const auto range = patch_lab::tweak_range(*base, tweak);
      const int target = std::clamp(
          *value + static_cast<int>(std::lround(sign * px / span *
                                                static_cast<float>(range.max))),
          range.min, range.max);
      return patch_lab::with_tweak(from, tweak, target);
    };
    auto edited = *base;
    switch (held) {
    case SketchHandle::Peak:
      // Right is a longer attack, a lower rate; up is louder.
      edited =
          step(edited, patch_lab::Tweak::Attack, moved.x, p.attack_span, -1.0f);
      edited =
          step(edited, patch_lab::Tweak::Volume, moved.y, p.level_span, -1.0f);
      break;
    case SketchHandle::Knee:
      // Right or up is a gentler decay, down or left a steeper and deeper one.
      edited = step(edited, patch_lab::Tweak::Decay, moved.y - moved.x,
                    p.decay_span + p.level_span, 1.0f);
      break;
    case SketchHandle::Release:
      edited = step(edited, patch_lab::Tweak::Release, moved.x, p.release_span,
                    -1.0f);
      break;
    case SketchHandle::None:
      break;
    }
    instrument = edited;
  }

  if (ImGui::IsItemDeactivated()) {
    if (held != SketchHandle::None) {
      base.reset();
      context.commit_history();
    }
    held = SketchHandle::None;
  }
  if (held != SketchHandle::None) {
    pointer.handle = held;
    pointer.held = true;
    return pointer;
  }
  if (!pointer.over) {
    return pointer;
  }
  // Under the pointer: the nearest handle within reach.
  const SketchPoints p = sketch_points(*shape, min, size);
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  SketchHandle near = SketchHandle::None;
  float best = ui::scale::px(kSketchHandleRadius * 3.0f);
  for (const auto &[handle, at] :
       {std::pair{SketchHandle::Peak, p.peak},
        std::pair{SketchHandle::Knee, p.knee},
        std::pair{SketchHandle::Release, p.release}}) {
    const float distance = std::hypot(mouse.x - at.x, mouse.y - at.y);
    if (distance <= best) {
      best = distance;
      near = handle;
    }
  }
  pointer.handle = near;
  return pointer;
}

void render_quick_edit_section(PatchEditorContext &context,
                               ym2612::Patch &patch) {
  if (!section_header("Quick Edit", context.prefs.show_quick_edit)) {
    return;
  }
  const float available = ImGui::GetContentRegionAvail().x;
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  auto &instrument = patch.instrument;
  // Beside the sliders while they keep a useful width, above them otherwise.
  const bool beside = available > ui::scale::px(420.0f);
  ImVec2 sketch_size(std::min(available * 0.4f, ui::scale::px(240.0f)), 0.0f);
  ImVec2 sketch_min;
  SketchPointer pointer;
  if (beside) {
    ImGui::PushItemWidth(available - sketch_size.x - spacing);
  } else {
    // Drawn once the sliders have said which of them is pointed at.
    sketch_size = ImVec2(available, ImGui::GetFrameHeight() * 3.0f);
    sketch_min = ImGui::GetCursorScreenPos();
    pointer =
        drag_quick_edit_sketch(context, instrument, sketch_min, sketch_size);
    ImGui::PushItemWidth(available);
  }
  static constexpr std::pair<const char *, patch_lab::Tweak> kSliders[] = {
      {"Volume", patch_lab::Tweak::Volume},
      {"Brightness", patch_lab::Tweak::Brightness},
      {"Attack", patch_lab::Tweak::Attack},
      {"Decay", patch_lab::Tweak::Decay},
      {"Release", patch_lab::Tweak::Release},
  };
  static_assert(std::size(kSliders) == kTweakCount);
  SliderState lit[kTweakCount]{};
  ImGui::BeginGroup();
  for (const auto &[label, tweak] : kSliders) {
    lit[static_cast<int>(tweak)] =
        render_quick_edit_slider(context, instrument, label, tweak);
  }
  ImGui::EndGroup();
  ImGui::PopItemWidth();

  if (beside) {
    sketch_size.y = ImGui::GetItemRectSize().y;
    ImGui::SameLine(0.0f, spacing);
    sketch_min = ImGui::GetCursorScreenPos();
    pointer =
        drag_quick_edit_sketch(context, instrument, sketch_min, sketch_size);
  }
  // What the handle pointed at moves is lit, on the sketch and on the
  // operators, as its sliders would.
  if (pointer.handle != SketchHandle::None) {
    const SliderState state =
        pointer.held ? SliderState::Active : SliderState::Hover;
    for (const auto &[label, tweak] : kSliders) {
      if (handle_moves(pointer.handle, tweak)) {
        lit[static_cast<int>(tweak)] = state;
        light_tweak_fields(context, instrument, tweak, state);
      }
    }
  }
  draw_quick_edit_sketch(instrument, lit, pointer, sketch_min, sketch_size);
  section_end("Quick Edit");
}

void render_mml_section(PatchEditorContext &context,
                        const ym2612::Patch &patch) {
  if (!section_header("MML", context.prefs.show_mml)) {
    return;
  }
  // As it is saved, so the comment names the LFO's rate. Made again only
  // when that changes, not every frame.
  const ym2612::Patch saved =
      ym2612::written(patch, context.session.playback());
  auto &ui = context.document_ui(context.document);
  if (!ui.mml_patch || *ui.mml_patch != saved) {
    ui.mml = formats::adapter::serialize_text(ym2612_format::Format::Mml, saved)
                 .value_or(std::string{});
    ui.mml_patch = saved;
  }
  const std::string &mml = ui.mml;
  if (ImGui::Button("Copy to Clipboard")) {
    platform::clipboard::copy_text(mml.c_str());
  }
  ImGui::PushStyleColor(ImGuiCol_Border,
                        styles::color(styles::MegatoyCol::PanelBorder));
  const bool visible =
      ImGui::BeginChild("##mml", ImVec2(0, 0),
                        ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
                        ImGuiWindowFlags_HorizontalScrollbar);
  ImGui::PopStyleColor();
  if (visible) {
    ImGui::TextUnformatted(mml.c_str());
  }
  ImGui::EndChild();
  section_end("MML");
}

} // namespace

void render_patch_editor_tools(PatchEditorContext &context,
                               ym2612::Patch &patch) {
  ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
  const bool side_by_side =
      ImGui::GetContentRegionAvail().x > ui::scale::px(800.0f);
  ImGui::Columns(side_by_side ? 2 : 1, "##tools_columns", false);
  render_quick_edit_section(context, patch);
  ImGui::NextColumn();
  render_mml_section(context, patch);
  ImGui::Columns(1);
}

} // namespace ui
