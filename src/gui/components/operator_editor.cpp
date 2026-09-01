#include "operator_editor.hpp"
#include "envelope_image.hpp"
#include "gui/components/operator_commands.hpp"
#include "gui/components/preview/ssg_preview.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include "patch_editor.hpp"
#include "ym2612/operator_edit.hpp"
#include "ym2612/types.hpp"
#include <algorithm>
#include <imgui.h>
#include <string>

namespace ui {

void text_centered(std::string text, float frame_width) {
  ImGui::Dummy(ImVec2(frame_width, ImGui::GetTextLineHeight()));

  ImVec2 text_size = ImGui::CalcTextSize(text.c_str());
  ImVec2 cursor_pos = ImGui::GetItemRectMin();
  cursor_pos.x += (frame_width - text_size.x) * 0.5f;

  ImGui::GetWindowDrawList()->AddText(
      cursor_pos, ImGui::GetColorU32(ImGuiCol_Text), text.c_str());
}

// Helper function to update slider state
inline void
update_slider_state(UIState::EnvelopeState::SliderState &slider_state) {
  if (ImGui::IsItemActive()) {
    slider_state = UIState::EnvelopeState::SliderState::Active;
  } else if (ImGui::IsItemHovered()) {
    slider_state = UIState::EnvelopeState::SliderState::Hover;
  } else {
    slider_state = UIState::EnvelopeState::SliderState::None;
  }
}

namespace {

// Room between an operator's border and its contents.
float frame_padding() { return ui::scale::px(6.0f); }
// Clearance either side of the header where the top border breaks for it.
float header_gap() { return ui::scale::px(4.0f); }

/// The envelope graph and the rate slider row beneath it, as one height.
float total_level_slider_height() {
  return vslider_height() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
}

/// What the TL column costs the graph beside it: one slider and the gap.
float total_level_column_width() {
  return vslider_width() + ImGui::GetStyle().ItemSpacing.x;
}

ym2612::MultiEditMode multi_edit_mode(const PatchEditorContext &context) {
  return context.prefs.multi_operator_edit_absolute
             ? ym2612::MultiEditMode::Absolute
             : ym2612::MultiEditMode::Relative;
}

/// Clicking replaces the selection; shift toggles, so a mis-picked operator
/// can come back out of what a paste is about to overwrite.
void note_operator_click(OperatorSelection &selection, int slot) {
  if (ImGui::GetIO().KeyShift) {
    selection.toggle_extend(slot);
  } else {
    selection.select_only(slot);
  }
}

/**
 * Editing keeps the group and only moves the lead; editing outside the
 * selection collapses onto it. Shift extends rather than toggles -- a
 * shift-drag must not drop the operator under the cursor mid-drag.
 */
void note_operator_edit(OperatorSelection &selection, int slot) {
  if (ImGui::GetIO().KeyShift) {
    selection.extend(slot);
  } else {
    selection.touch(slot);
  }
}

/**
 * Border colour by state. Selected is the lead's highlight faded rather than
 * a grey, which sat too close to the unselected borders to register. Hover
 * only shows on unselected operators, where it advertises the click.
 */
ImU32 operator_frame_color(bool selected, bool is_primary, bool hovered) {
  const ImVec4 highlight = styles::color(styles::MegatoyCol::TextHighlight);
  if (is_primary) {
    return ImGui::GetColorU32(highlight);
  }
  if (selected) {
    return ImGui::GetColorU32(
        ImVec4(highlight.x, highlight.y, highlight.z, highlight.w * 0.5f));
  }
  if (hovered) {
    return ImGui::GetColorU32(ImGuiCol_SeparatorHovered);
  }
  return ImGui::GetColorU32(ImGuiCol_Border);
}

/**
 * The frame, cut open at the corner the header sits across: the top border
 * resumes past the label, the left border below the checkbox.
 *
 * The half-pixel inset is what AddRect does internally; without it the line
 * falls between two pixels and blurs.
 */
void draw_operator_frame(const ImVec2 &min, const ImVec2 &max,
                         float top_resume_x, float left_resume_y, ImU32 color) {
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  const float left = min.x + 0.5f;
  const float top = min.y + 0.5f;
  const float right = max.x - 0.5f;
  const float bottom = max.y - 0.5f;

  if (top_resume_x < right) {
    draw_list->AddLine(ImVec2(top_resume_x, top), ImVec2(right, top), color);
  }
  if (left_resume_y < bottom) {
    draw_list->AddLine(ImVec2(left, left_resume_y), ImVec2(left, bottom),
                       color);
  }
  draw_list->AddLine(ImVec2(right, top), ImVec2(right, bottom), color);
  draw_list->AddLine(ImVec2(left, bottom), ImVec2(right, bottom), color);
}

struct FieldLabels {
  const char *name;
  /// Undo merge key; unchanged from before so drags still merge.
  const char *key;
};

FieldLabels field_labels(ym2612::OperatorField field) {
  switch (field) {
  case ym2612::OperatorField::TotalLevel:
    return {"Total Level", "total_level"};
  case ym2612::OperatorField::AttackRate:
    return {"Attack Rate", "attack_rate"};
  case ym2612::OperatorField::DecayRate:
    return {"Decay Rate", "decay_rate"};
  case ym2612::OperatorField::SustainLevel:
    return {"Sustain Level", "sustain_level"};
  case ym2612::OperatorField::SustainRate:
    return {"Sustain Rate", "sustain_rate"};
  case ym2612::OperatorField::ReleaseRate:
    return {"Release Rate", "release_rate"};
  case ym2612::OperatorField::KeyScale:
    return {"Key Scale", "key_scale"};
  case ym2612::OperatorField::Multiple:
    return {"Multiple", "multiple"};
  case ym2612::OperatorField::Detune:
    return {"Detune", "detune"};
  case ym2612::OperatorField::SsgType:
    return {"SSG EG Type", "ssg_type"};
  }
  return {"", ""};
}

std::string history_key(int slot, const char *suffix) {
  return "instrument.op" + std::to_string(slot) + "." + suffix;
}

/// "OP1 Total Level", or "OP1, OP3 Total Level" with several selected, so
/// the Edit menu's "Undo ..." says how far the change reached.
std::string history_label(const OperatorSelection &selection, int slot,
                          const char *name) {
  const std::string who = selection.contains(slot) && selection.count() > 1
                              ? operator_selection_label(selection)
                              : operator_slot_label(slot);
  return who + " " + name;
}

struct OperatorWidget {
  PatchEditorContext &editor;
  ym2612::ChannelInstrument &instrument;
  int slot;
};

/**
 * One operator parameter, with the selection, the relative spreading and the
 * undo history in a single place rather than repeated per slider.
 *
 * `vertical_size` picks a VSliderInt, and those hide their label, so they are
 * the ones that get a tooltip.
 */
void operator_slider(OperatorWidget &widget, ym2612::OperatorField field,
                     const char *id, const ImVec2 *vertical_size,
                     const char *value_format,
                     UIState::EnvelopeState::SliderState *slider_state) {
  auto &state = widget.editor.operator_edit;
  auto &op = ym2612::operator_at(widget.instrument, widget.slot);
  const auto range = ym2612::operator_field_range(field);
  const auto labels = field_labels(field);

  int value = ym2612::read_operator_field(op, field);
  const bool changed =
      vertical_size
          ? ImGui::VSliderInt(id, *vertical_size, &value, range.max, range.min)
          : ImGui::SliderInt(id, &value, range.min, range.max, value_format);
  if (slider_state) {
    update_slider_state(*slider_state);
  }

  // Order matters on the frame a drag starts: the selection settles, then
  // the baseline is captured, and only then is the new value written.
  if (ImGui::IsItemActivated()) {
    note_operator_edit(state.selection, widget.slot);
    ym2612::capture_operator_baseline(state.baseline, widget.instrument, field,
                                      widget.slot);
  }

  track_patch_history(widget.editor,
                      history_label(state.selection, widget.slot, labels.name),
                      history_key(widget.slot, labels.key));

  if (changed) {
    ym2612::apply_operator_field_edit(
        widget.instrument, state.selection.selected, state.baseline, field,
        widget.slot, value, multi_edit_mode(widget.editor));
  }
  if (ImGui::IsItemDeactivated()) {
    state.baseline.clear();
  }

  if (vertical_size && ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", labels.name);
  }
}

/**
 * A per-operator flag. Booleans have no delta, so spreading one is absolute
 * in both modes. `spread` is off for the enable flag: it is a mute, and
 * dragging the others along would undo the audition it was set up for.
 */
void operator_checkbox(OperatorWidget &widget, const char *id, const char *name,
                       bool ym2612::OperatorSettings::*flag, bool spread) {
  auto &state = widget.editor.operator_edit;
  auto &op = ym2612::operator_at(widget.instrument, widget.slot);

  bool value = op.*flag;
  const bool changed = ImGui::Checkbox(id, &value);
  if (ImGui::IsItemActivated()) {
    note_operator_edit(state.selection, widget.slot);
  }
  if (changed) {
    track_instant_patch_history(
        widget.editor, history_label(state.selection, widget.slot, name), [&] {
          if (spread) {
            ym2612::apply_operator_flag_edit(
                widget.instrument, state.selection.selected, flag, value);
          } else {
            op.*flag = value;
          }
        });
  }
}

/// The operator's enable checkbox and name, sitting in the frame's top
/// border. Returns the x where the border can pick up again.
float render_operator_header(OperatorWidget &widget, uint8_t algorithm,
                             const ImVec2 &header_min) {
  const int slot = widget.slot;
  // Carriers reach the output; modulators only feed other operators. Which
  // is which moves with the algorithm, so it is worth saying on every one.
  const bool is_modulator = ym2612::operator_register_index(slot) <
                            ym2612::algorithm_modulator_count[algorithm];

  ImGui::SetCursorScreenPos(header_min);
  ImGui::BeginGroup();
  operator_checkbox(widget, "##Operator Enable", "Enable",
                    &ym2612::OperatorSettings::enable, false);
  ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
  if (is_modulator) {
    ImGui::TextUnformatted(operator_slot_label(slot).c_str());
  } else {
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive),
                       "%s (Carrier)", operator_slot_label(slot).c_str());
  }
  ImGui::EndGroup();
  return ImGui::GetItemRectMax().x;
}

/**
 * TL's own column, the label above a slider as tall as the envelope graph and
 * the rate slider row together.
 *
 * TL runs 0..127 where the rate sliders run 0..31 and 0..15, so it takes both
 * rows' height for one step to stay wider than a pixel under the mouse. It
 * sits beside the graph rather than under it because the graph's vertical
 * axis is attenuation and TL is the operator's output level: the two read as
 * one axis.
 */
void render_total_level_slider(OperatorWidget &widget,
                               UIState::EnvelopeState &envelope_state) {
  const ImVec2 slider_size(vslider_width(), total_level_slider_height());

  ImGui::BeginGroup();
  text_centered("TL", slider_size.x);
  operator_slider(widget, ym2612::OperatorField::TotalLevel, "##Total Level",
                  &slider_size, nullptr, &envelope_state.total_level);
  ImGui::EndGroup();
}

/**
 * The five rate sliders under the graph, labelled.
 *
 * Five 20 px columns and the gaps between them, which together with the TL
 * column beside them comes to hslider_width(). The graph above is wider than
 * this in the two-column layout, and the sliders deliberately do not follow
 * it -- widening them would claim they share the graph's axis, and a 20 px
 * column is the width these are grabbed at.
 */
void render_envelope_sliders(OperatorWidget &widget,
                             UIState::EnvelopeState &envelope_state) {
  const ImVec2 slider_size = vslider_size();
  const float label_width = slider_size.x;

  ImGui::BeginGroup(); // ADSR slider block

  ImGui::BeginGroup();
  text_centered("AR", label_width);
  ImGui::SameLine();
  text_centered("DR", label_width);
  ImGui::SameLine();
  text_centered("SL", label_width);
  ImGui::SameLine();
  text_centered("SR", label_width);
  ImGui::SameLine();
  text_centered("RR", label_width);
  ImGui::EndGroup();

  ImGui::BeginGroup();
  operator_slider(widget, ym2612::OperatorField::AttackRate, "##Attack Rate",
                  &slider_size, nullptr, &envelope_state.attack_rate);
  ImGui::SameLine();
  operator_slider(widget, ym2612::OperatorField::DecayRate, "##Decay Rate",
                  &slider_size, nullptr, &envelope_state.decay_rate);
  ImGui::SameLine();
  operator_slider(widget, ym2612::OperatorField::SustainLevel,
                  "##Sustain Level", &slider_size, nullptr,
                  &envelope_state.sustain_level);
  ImGui::SameLine();
  operator_slider(widget, ym2612::OperatorField::SustainRate, "##Sustain Rate",
                  &slider_size, nullptr, &envelope_state.sustain_rate);
  ImGui::SameLine();
  operator_slider(widget, ym2612::OperatorField::ReleaseRate, "##Release Rate",
                  &slider_size, nullptr, &envelope_state.release_rate);
  ImGui::EndGroup();

  ImGui::EndGroup(); // End ADSR slider block
}

void render_operator_contents(PatchEditorContext &context, ym2612::Patch &patch,
                              int slot, UIState::EnvelopeState &envelope_state,
                              bool space_for_feedback,
                              const EnvelopeVoices &voices) {
  auto &state = context.operator_edit;
  auto &instrument = patch.instrument;
  auto &op = ym2612::operator_at(instrument, slot);
  OperatorWidget widget{context, instrument, slot};

  // Two columns only when the right-hand one fits whole. Its widest row is a
  // full-width slider plus its label, and the gap between the columns is the
  // SameLine/Spacing/SameLine below.
  const ImGuiStyle &style = ImGui::GetStyle();
  const float content_width = ImGui::GetContentRegionAvail().x;
  const float side_column_width = hslider_width() + style.ItemInnerSpacing.x +
                                  ImGui::CalcTextSize("SSG EG Type").x;
  const float two_column_width =
      hslider_width() + style.ItemSpacing.x * 2.0f + side_column_width;
  const bool column_layout = content_width > two_column_width;

  if (!op.enable) {
    ImGui::BeginDisabled();
  }
  ImGui::PushItemWidth(hslider_width());

  if (slot == 0) {
    // Feedback belongs to the channel, not to an operator -- there is one of
    // it, and OP1's panel is only where it is drawn. So it spreads nowhere,
    // but it still leads: editing it makes OP1 the primary operator.
    int feedback = instrument.feedback;
    const bool feedback_changed = ImGui::SliderInt("Feedback", &feedback, 0, 7);
    if (ImGui::IsItemActivated()) {
      note_operator_edit(state.selection, 0);
    }
    track_patch_history(context, "OP1 Feedback", "instrument.feedback");
    if (feedback_changed) {
      instrument.feedback = static_cast<uint8_t>(feedback);
    }
  } else if (space_for_feedback) {
    // Stand in for the Feedback slider OP1 has and the others do not, so all
    // four line up.
    ImVec2 feedback_gap = ImGui::GetCursorPos();
    ImGui::SetCursorPosY(feedback_gap.y + ImGui::GetFrameHeightWithSpacing());
  }

  operator_checkbox(
      widget, "Amplitude Modulation Enable", "Amplitude Modulation",
      &ym2612::OperatorSettings::amplitude_modulation_enable, true);
  ImGui::Spacing();

  // TL runs down the left of the panel in every layout; the graph and the rate
  // sliders start to the right of it.
  render_total_level_slider(widget, envelope_state);
  ImGui::SameLine();

  // The graph is what the panel is read for, so it spans what the TL column
  // leaves in every layout, with the rate sliders underneath. frame_padding()
  // comes off the right so it clears the border by as much as the contents do
  // on the left. Only the pixels change: the time axis is unaffected.
  ImGui::BeginGroup();
  render_envelope_image(
      op, envelope_state,
      ImVec2(content_width - frame_padding() - total_level_column_width(),
             vslider_height()),
      voices);
  const float sliders_top = ImGui::GetCursorPosY();
  render_envelope_sliders(widget, envelope_state);
  ImGui::EndGroup();

  if (column_layout) {
    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();
    // Level with the vertical sliders rather than with the graph, so the SSG
    // controls sit straight above Key Scale.
    ImGui::SetCursorPosY(sliders_top);
    ImGui::BeginGroup();
  } else {
    ImGui::Spacing();
  }

  // SSG Type Envelope Control (0-7)
  const int ssg_type = op.ssg_type_envelope_control;
  if (const auto *preview = op.ssg_enable ? get_ssg_preview_texture(ssg_type)
                                          : get_ssg_preview_off_texture()) {
    if (preview->valid()) {
      ImGui::Image(preview->texture_id, ui::scale::px(preview->size));
    }
  }
  ImGui::SameLine();

  operator_checkbox(widget, "SSG EG Enable", "SSG EG Enable",
                    &ym2612::OperatorSettings::ssg_enable, true);

  const bool ssg_enable = op.ssg_enable;
  if (!ssg_enable) {
    ImGui::BeginDisabled();
  }
  operator_slider(widget, ym2612::OperatorField::SsgType, "SSG EG Type",
                  nullptr, nullptr, nullptr);
  if (!ssg_enable) {
    ImGui::EndDisabled();
  }

  // Only the single-column layout needs a break here: in the two-column one
  // the SSG controls already stand clear of the sliders beside them.
  if (!column_layout) {
    ImGui::Spacing();
  }

  operator_slider(widget, ym2612::OperatorField::KeyScale, "Key Scale", nullptr,
                  nullptr, nullptr);

  static const char *multiple_labels[] = {"0.5", "1",  "2",  "3", "4",  "5",
                                          "6",   "7",  "8",  "9", "10", "11",
                                          "12",  "13", "14", "15"};
  operator_slider(widget, ym2612::OperatorField::Multiple, "Multiple", nullptr,
                  multiple_labels[op.multiple], nullptr);

  static const char *detune_labels[] = {"-3", "-2", "-1", "0", "1", "2", "3"};
  operator_slider(widget, ym2612::OperatorField::Detune, "Detune", nullptr,
                  detune_labels[ym2612::read_operator_field(
                      op, ym2612::OperatorField::Detune)],
                  nullptr);

  if (column_layout) {
    ImGui::EndGroup();
  }
  ImGui::PopItemWidth();

  if (!op.enable) {
    ImGui::EndDisabled();
  }
}

} // namespace

void render_operator_editor(PatchEditorContext &context, ym2612::Patch &patch,
                            int slot, UIState::EnvelopeState &envelope_state,
                            bool space_for_feedback,
                            const EnvelopeVoices &voices) {
  auto &state = context.operator_edit;
  OperatorWidget widget{context, patch.instrument, slot};

  ImGui::PushID(slot);

  const ImVec2 block_min = ImGui::GetCursorScreenPos();
  // Not GetColumnWidth(): a column clips its draw list tighter than that,
  // and the right border falls outside.
  const float frame_width = std::max(ImGui::GetContentRegionAvail().x, 1.0f);

  // The header straddles the top border, aligned with the sliders in the
  // sections above rather than indented into the frame.
  const float header_height = ImGui::GetFrameHeight();
  const ImVec2 frame_min(block_min.x, block_min.y + header_height * 0.5f);
  const float header_end =
      render_operator_header(widget, patch.instrument.algorithm, block_min);
  const float top_resume_x =
      std::min(header_end + header_gap(), frame_min.x + frame_width);
  const float left_resume_y = block_min.y + header_height + header_gap();

  ImGui::SetCursorScreenPos(
      ImVec2(frame_min.x + frame_padding(),
             block_min.y + header_height + frame_padding()));
  ImGui::BeginGroup();
  render_operator_contents(context, patch, slot, envelope_state,
                           space_for_feedback, voices);
  ImGui::EndGroup();

  const float content_height =
      ImGui::GetItemRectMax().y - frame_min.y + frame_padding();
  state.pending_frame_height =
      std::max(state.pending_frame_height, content_height);
  // All four drawn to the tallest one's height so the borders line up. From
  // the previous frame; measuring and applying in one would need two passes.
  const float frame_height = std::max(content_height, state.frame_height);
  const ImVec2 frame_max(frame_min.x + frame_width, frame_min.y + frame_height);
  // Claim the full block so the columns row and the scroll extent account
  // for the header, the padding and any borrowed height.
  ImGui::SetCursorScreenPos(ImVec2(block_min.x, frame_max.y));

  // By rectangle rather than an invisible button underneath, which would
  // have to yield hover to every widget on top of it -- IsAnyItemHovered
  // reports that directly.
  const bool window_hovered =
      ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
  // From block_min: the header sits above the border, and clicking beside
  // the label should select too.
  const bool frame_hovered =
      window_hovered && ImGui::IsMouseHoveringRect(block_min, frame_max);
  const bool on_background = frame_hovered && !ImGui::IsAnyItemHovered();
  // Not while something is being dragged, so a slider drag that wanders out
  // does not light up the operator it passes over.
  const bool background_hovered = on_background && !ImGui::IsAnyItemActive();

  if (on_background && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    note_operator_click(state.selection, slot);
    state.click_claimed = true;
  }

  // Anywhere in the frame, including on a slider, which ignores right-click.
  if (frame_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    if (!state.selection.contains(slot)) {
      state.selection.select_only(slot);
    }
    state.click_claimed = true;
    ImGui::OpenPopup("##operator_menu");
  }
  if (ImGui::BeginPopup("##operator_menu")) {
    OperatorCommandContext commands{
        patch.instrument, state,
        [&context](const std::string &label) {
          if (context.begin_history) {
            context.begin_history(label, {}, context.session.current_patch());
          }
        },
        [&context]() {
          if (context.commit_history) {
            context.commit_history();
          }
        }};
    render_operator_command_items(commands);
    ImGui::EndPopup();
  }

  draw_operator_frame(frame_min, frame_max, top_resume_x, left_resume_y,
                      operator_frame_color(state.selection.contains(slot),
                                           state.selection.primary == slot,
                                           background_hovered));

  ImGui::PopID();
}

} // namespace ui
