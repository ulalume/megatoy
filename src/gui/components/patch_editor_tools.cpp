#include "patch_editor_tools.hpp"

#include "formats/ym2612_format_adapter.hpp"
#include "gui/clipboard.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include "patches/patch_tweak.hpp"
#include <imgui.h>
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
    auto &macro = context.envelope_state(slot).macro;
    const auto &moved = fields[static_cast<std::size_t>(slot)];
    const auto raise = [lit](SliderState &field, bool on) {
      if (on) {
        field = UIState::EnvelopeState::stronger(field, lit);
      }
    };
    raise(macro.total_level, moved.total_level);
    raise(macro.attack_rate, moved.attack_rate);
    raise(macro.decay_rate, moved.decay_rate);
    raise(macro.sustain_level, moved.sustain_level);
    raise(macro.sustain_rate, moved.sustain_rate);
    raise(macro.release_rate, moved.release_rate);
  }
}

void render_macro_slider(PatchEditorContext &context,
                         ym2612::ChannelInstrument &instrument,
                         const char *label, patch_lab::Tweak tweak) {
  auto &base = context.document_ui(context.document).macro_base;
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
    context.begin_history(std::string("Macro ") + label, {});
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

void render_macro_section(PatchEditorContext &context, ym2612::Patch &patch) {
  ImGui::SetNextItemOpen(context.prefs.show_macro, ImGuiCond_Always);
  context.prefs.show_macro = ImGui::CollapsingHeader("Macro");
  if (!context.prefs.show_macro) {
    return;
  }
  const float label_width = ImGui::CalcTextSize("Brightness").x +
                            ImGui::GetStyle().ItemInnerSpacing.x;
  ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - label_width);
  auto &instrument = patch.instrument;
  render_macro_slider(context, instrument, "Volume", patch_lab::Tweak::Volume);
  render_macro_slider(context, instrument, "Brightness",
                      patch_lab::Tweak::Brightness);
  render_macro_slider(context, instrument, "Attack", patch_lab::Tweak::Attack);
  render_macro_slider(context, instrument, "Decay", patch_lab::Tweak::Decay);
  render_macro_slider(context, instrument, "Release",
                      patch_lab::Tweak::Release);
  ImGui::PopItemWidth();
}

void render_mml_section(PatchEditorContext &context,
                        const ym2612::Patch &patch) {
  ImGui::SetNextItemOpen(context.prefs.show_mml, ImGuiCond_Always);
  context.prefs.show_mml = ImGui::CollapsingHeader("MML");
  if (!context.prefs.show_mml) {
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
  const bool side_by_side =
      ImGui::GetContentRegionAvail().x > ui::scale::px(800.0f);
  ImGui::Columns(side_by_side ? 2 : 1, "##tools_columns", false);
  render_macro_section(context, patch);
  ImGui::NextColumn();
  render_mml_section(context, patch);
  ImGui::Columns(1);
}

} // namespace ui
