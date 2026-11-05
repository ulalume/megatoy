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

void render_quick_edit_slider(PatchEditorContext &context,
                              ym2612::ChannelInstrument &instrument,
                              const char *label, patch_lab::Tweak tweak) {
  auto &base = context.document_ui(context.document).quick_edit_base;
  const auto value = patch_lab::tweak_value(instrument, tweak);
  const auto range = patch_lab::tweak_range(base.value_or(instrument), tweak);
  const auto text = patch_lab::tweak_text(instrument, tweak);

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

  if (ImGui::IsItemActive()) {
    light_tweak_fields(context, instrument, tweak, SliderState::Active);
  } else if (ImGui::IsItemHovered()) {
    light_tweak_fields(context, instrument, tweak, SliderState::Hover);
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
}

void render_quick_edit_section(PatchEditorContext &context,
                               ym2612::Patch &patch) {
  if (!section_header("Quick Edit", context.prefs.show_quick_edit)) {
    return;
  }
  ImGui::TextColored(styles::color(styles::MegatoyCol::TextMuted),
                     "Moves the operators together, keeping their balance.");
  const float label_width = ImGui::CalcTextSize("Brightness").x +
                            ImGui::GetStyle().ItemInnerSpacing.x;
  ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - label_width);
  auto &instrument = patch.instrument;
  render_quick_edit_slider(context, instrument, "Volume",
                           patch_lab::Tweak::Volume);
  render_quick_edit_slider(context, instrument, "Brightness",
                           patch_lab::Tweak::Brightness);
  render_quick_edit_slider(context, instrument, "Attack",
                           patch_lab::Tweak::Attack);
  render_quick_edit_slider(context, instrument, "Decay",
                           patch_lab::Tweak::Decay);
  render_quick_edit_slider(context, instrument, "Release",
                           patch_lab::Tweak::Release);
  ImGui::PopItemWidth();
}

void render_mml_section(PatchEditorContext &context,
                        const ym2612::Patch &patch) {
  if (!section_header("MML", context.prefs.show_mml)) {
    return;
  }
  const auto mml =
      formats::adapter::serialize_text(ym2612_format::Format::Mml, patch)
          .value_or(std::string{});
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
