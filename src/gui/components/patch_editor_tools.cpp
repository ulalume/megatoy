#include "patch_editor_tools.hpp"

#include "formats/ym2612_format_adapter.hpp"
#include "gui/clipboard.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include "patches/patch_tweak.hpp"
#include "ym2612/operator_edit.hpp"
#include <algorithm>
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

/// A bar that opens and closes the section under it, as wide as the items
/// around it. Returns whether the section is open.
bool section_header(const char *label, bool &open) {
  const ImGuiStyle &style = ImGui::GetStyle();
  const ImVec2 min = ImGui::GetCursorScreenPos();
  const ImVec2 size(std::max(ImGui::GetContentRegionAvail().x, 1.0f),
                    ImGui::GetFrameHeight());
  if (ImGui::InvisibleButton(label, size)) {
    open = !open;
  }
  const auto col =
      ImGui::IsItemActive()    ? styles::MegatoyCol::SectionHeaderActive
      : ImGui::IsItemHovered() ? styles::MegatoyCol::SectionHeaderHovered
                               : styles::MegatoyCol::SectionHeader;
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  const ImVec2 max(min.x + size.x, min.y + size.y);
  draw_list->AddRectFilled(min, max, styles::color_u32(col),
                           style.FrameRounding);
  const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
  const float font_size = ImGui::GetFontSize();
  ImGui::RenderArrow(
      draw_list,
      ImVec2(min.x + style.FramePadding.x, min.y + style.FramePadding.y), text,
      open ? ImGuiDir_Down : ImGuiDir_Right, 1.0f);
  draw_list->AddText(ImVec2(min.x + style.FramePadding.x * 2.0f + font_size,
                            min.y + style.FramePadding.y),
                     text, label);
  return open;
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
  const auto text = base && ImGui::GetActiveID() == ImGui::GetID(label)
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
      ImGui::SliderInt(label, &shown, longer_first ? range.max : range.min,
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

/**
 * A sketch of the envelope the sliders move, drawn the way an operator's
 * graph is: the held line, and the release as an area from the start. Not
 * to scale; it shows which way each slider takes the shape.
 */
void draw_quick_edit_sketch(const ym2612::ChannelInstrument &instrument,
                            const SliderState (&lit)[kTweakCount],
                            const ImVec2 &min, const ImVec2 &size) {
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

  const auto is_lit = [&lit](patch_lab::Tweak tweak) {
    return lit[static_cast<int>(tweak)] != SliderState::None;
  };
  const auto curve_color = [&](patch_lab::Tweak tweak) {
    return styles::color_u32(is_lit(tweak) || is_lit(patch_lab::Tweak::Volume)
                                 ? styles::MegatoyCol::EnvelopeCurveActive
                                 : styles::MegatoyCol::EnvelopeCurve);
  };

  const float pad = ui::scale::px(4.0f);
  const float left = min.x + pad;
  const float right = max.x - pad;
  const float top = min.y + pad;
  const float floor = max.y - pad;
  const float width = right - left;
  const float height = floor - top;
  // Never flat on the floor, so a quiet patch still has a shape to show.
  const float peak = floor - height * (0.15f + 0.85f * shape->level);
  const float attack_end = left + width * (0.02f + 0.22f * shape->attack);
  const float decay_end = attack_end + width * (0.03f + 0.28f * shape->decay);
  const float sustain = floor - (floor - peak) * shape->sustain;
  const float held_end = sustain + (floor - sustain) * shape->hold;
  const float release_end =
      std::min(right, left + width * (0.04f + 0.9f * shape->release));

  draw_list->AddTriangleFilled(
      ImVec2(left, peak), ImVec2(release_end, floor), ImVec2(left, floor),
      styles::color_u32(is_lit(patch_lab::Tweak::Release)
                            ? styles::MegatoyCol::EnvelopeReleaseActive
                            : styles::MegatoyCol::EnvelopeRelease));

  const float thickness = ui::scale::px(1.5f);
  draw_list->AddLine(ImVec2(left, floor), ImVec2(attack_end, peak),
                     curve_color(patch_lab::Tweak::Attack), thickness);
  draw_list->AddLine(ImVec2(attack_end, peak), ImVec2(decay_end, sustain),
                     curve_color(patch_lab::Tweak::Decay), thickness);
  draw_list->AddLine(ImVec2(decay_end, sustain), ImVec2(right, held_end),
                     curve_color(patch_lab::Tweak::Volume), thickness);
}

void render_quick_edit_section(PatchEditorContext &context,
                               ym2612::Patch &patch) {
  if (!section_header("Quick Edit", context.prefs.show_quick_edit)) {
    return;
  }
  const float available = ImGui::GetContentRegionAvail().x;
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  // Beside the sliders while they keep a useful width, above them otherwise.
  const bool beside = available > ui::scale::px(420.0f);
  const float label_width = ImGui::CalcTextSize("Brightness").x +
                            ImGui::GetStyle().ItemInnerSpacing.x;
  ImVec2 sketch_size(std::min(available * 0.4f, ui::scale::px(240.0f)), 0.0f);
  ImVec2 sketch_min;
  if (beside) {
    ImGui::PushItemWidth(available - label_width - sketch_size.x - spacing);
  } else {
    // Drawn once the sliders have said which of them is pointed at.
    sketch_size = ImVec2(available, ImGui::GetFrameHeight() * 3.0f);
    sketch_min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(sketch_size);
    ImGui::PushItemWidth(available - label_width);
  }
  auto &instrument = patch.instrument;
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
    ImGui::Dummy(sketch_size);
  }
  draw_quick_edit_sketch(instrument, lit, sketch_min, sketch_size);
}

void render_mml_section(PatchEditorContext &context,
                        const ym2612::Patch &patch) {
  if (!section_header("MML", context.prefs.show_mml)) {
    return;
  }
  // Made again only when the patch changes, not every frame.
  auto &ui = context.document_ui(context.document);
  if (!ui.mml_patch || *ui.mml_patch != patch) {
    ui.mml = formats::adapter::serialize_text(ym2612_format::Format::Mml, patch)
                 .value_or(std::string{});
    ui.mml_patch = patch;
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
