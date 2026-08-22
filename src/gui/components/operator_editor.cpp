#include "operator_editor.hpp"
#include "envelope_image.hpp"
#include "gui/components/operator_commands.hpp"
#include "gui/components/preview/ssg_preview.hpp"
#include "gui/styles/megatoy_style.hpp"
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
constexpr float frame_padding = 6.0f;
// Keeps neighbouring operators' borders from touching in a columns layout.
constexpr float frame_gutter = 6.0f;

ym2612::MultiEditMode multi_edit_mode(const PatchEditorContext &context) {
  return context.prefs.multi_operator_edit_absolute
             ? ym2612::MultiEditMode::Absolute
             : ym2612::MultiEditMode::Relative;
}

/**
 * Clicking an operator replaces the selection; shift toggles it.
 *
 * Toggling is what lets a mis-picked operator come back out of a
 * shift-selection, which matters when the selection is what a paste is about
 * to overwrite.
 */
void note_operator_click(OperatorSelection &selection, int slot) {
  if (ImGui::GetIO().KeyShift) {
    selection.toggle_extend(slot);
  } else {
    selection.select_only(slot);
  }
}

/**
 * Editing a widget keeps a multi-operator selection together and only moves
 * the lead; editing an operator outside the selection collapses onto it.
 *
 * Shift extends here rather than toggling: a shift-drag must not take the
 * operator under the cursor back out of the selection halfway through.
 */
void note_operator_edit(OperatorSelection &selection, int slot) {
  if (ImGui::GetIO().KeyShift) {
    selection.extend(slot);
  } else {
    selection.touch(slot);
  }
}

/**
 * Border colour by state. Unselected is the same near-black line the Patch
 * Lab results panel is drawn with; hover only shows on unselected operators,
 * where it is advertising that the frame can be clicked at all.
 */
ImU32 operator_frame_color(bool selected, bool is_primary, bool hovered) {
  if (is_primary) {
    return ImGui::GetColorU32(
        styles::color(styles::MegatoyCol::TextHighlight));
  }
  if (selected) {
    return ImGui::GetColorU32(ImGuiCol_Separator);
  }
  if (hovered) {
    return ImGui::GetColorU32(ImGuiCol_SeparatorHovered);
  }
  return ImGui::GetColorU32(ImGuiCol_Border);
}

struct FieldLabels {
  const char *name;
  // Kept identical to the keys the editor has always used, so a drag started
  // before this change and one started after still merge into one undo step.
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

/// "OP1 Total Level", or "OP1, OP3 Total Level" while several are selected,
/// so the Edit menu's "Undo ..." says how far the change reached.
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
 * One operator parameter, wired to the selection, to relative spreading and
 * to the undo history in a single place. Ten near-identical slider blocks
 * used to repeat all three by hand.
 *
 * `vertical_size` picks a VSliderInt over a SliderInt, and those are the
 * ones with hidden labels, so they are also the ones that get a tooltip.
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

  // Order matters on the frame a drag starts. The selection has to settle
  // before the baseline is captured, and the baseline has to be read before
  // the new value is written -- otherwise the primary operator's own
  // starting point is already gone and its delta comes out as zero.
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
 * A per-operator flag. Booleans have no meaningful delta, so a spread one is
 * absolute in both edit modes.
 *
 * `spread` is off for the operator's own enable flag: that one is a mute set
 * up to audition part of a patch, and dragging three others along with it
 * would undo the thing the user just arranged.
 */
void operator_checkbox(OperatorWidget &widget, const char *id,
                       const char *name, const char *key,
                       bool ym2612::OperatorSettings::*flag, bool spread) {
  auto &state = widget.editor.operator_edit;
  auto &op = ym2612::operator_at(widget.instrument, widget.slot);

  bool value = op.*flag;
  const bool changed = ImGui::Checkbox(id, &value);
  if (ImGui::IsItemActivated()) {
    note_operator_edit(state.selection, widget.slot);
  }
  track_patch_history(widget.editor,
                      history_label(state.selection, widget.slot, name),
                      history_key(widget.slot, key));
  if (changed) {
    if (spread) {
      ym2612::apply_operator_flag_edit(widget.instrument,
                                       state.selection.selected, flag, value);
    } else {
      op.*flag = value;
    }
  }
}

void render_envelope(OperatorWidget &widget,
                     UIState::EnvelopeState &envelope_state) {
  const auto &op = ym2612::operator_at(widget.instrument, widget.slot);

  ImGui::BeginGroup(); // ADSR Envelope group
  render_envelope_image(op, envelope_state, image_size);

  ImGui::BeginGroup();
  text_centered("TL", vslider_width);
  ImGui::SameLine();
  text_centered("AR", vslider_width);
  ImGui::SameLine();
  text_centered("DR", vslider_width);
  ImGui::SameLine();
  text_centered("SL", vslider_width);
  ImGui::SameLine();
  text_centered("SR", vslider_width);
  ImGui::SameLine();
  text_centered("RR", vslider_width);
  ImGui::EndGroup();

  ImGui::BeginGroup();
  operator_slider(widget, ym2612::OperatorField::TotalLevel, "##Total Level",
                  &vslider_size, nullptr, &envelope_state.total_level);
  ImGui::SameLine();
  operator_slider(widget, ym2612::OperatorField::AttackRate, "##Attack Rate",
                  &vslider_size, nullptr, &envelope_state.attack_rate);
  ImGui::SameLine();
  operator_slider(widget, ym2612::OperatorField::DecayRate, "##Decay Rate",
                  &vslider_size, nullptr, &envelope_state.decay_rate);
  ImGui::SameLine();
  operator_slider(widget, ym2612::OperatorField::SustainLevel,
                  "##Sustain Level", &vslider_size, nullptr,
                  &envelope_state.sustain_level);
  ImGui::SameLine();
  operator_slider(widget, ym2612::OperatorField::SustainRate, "##Sustain Rate",
                  &vslider_size, nullptr, &envelope_state.sustain_rate);
  ImGui::SameLine();
  operator_slider(widget, ym2612::OperatorField::ReleaseRate, "##Release Rate",
                  &vslider_size, nullptr, &envelope_state.release_rate);
  ImGui::EndGroup();

  ImGui::EndGroup(); // End ADSR Envelope group
}

void render_operator_contents(PatchEditorContext &context, ym2612::Patch &patch,
                              int slot, UIState::EnvelopeState &envelope_state,
                              bool space_for_feedback) {
  auto &state = context.operator_edit;
  auto &instrument = patch.instrument;
  auto &op = ym2612::operator_at(instrument, slot);
  OperatorWidget widget{context, instrument, slot};

  const auto column_layout = ImGui::GetContentRegionAvail().x > 410.0f;
  const bool is_modulator =
      ym2612::operator_register_index(slot) <
      ym2612::algorithm_modulator_count[instrument.algorithm];
  const std::string op_label =
      operator_slot_label(slot) + (is_modulator ? "" : " (Carrier)");

  if (!is_modulator) {
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
    ImGui::PushStyleColor(ImGuiCol_Separator,
                          ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
  }
  ImVec2 pos = ImGui::GetCursorPos();
  ImGui::SeparatorText(op_label.c_str());
  ImGui::SetCursorPosY(pos.y);

  operator_checkbox(widget, "##Operator Enable", "Enable", "op_enable",
                    &ym2612::OperatorSettings::enable, false);

  if (!is_modulator) {
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
  }

  if (!op.enable) {
    ImGui::BeginDisabled();
  }
  ImGui::PushItemWidth(hslider_width);

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
    ImVec2 feedback_gap = ImGui::GetCursorPos();
    ImGui::SetCursorPosY(feedback_gap.y + 20);
  }

  operator_checkbox(widget, "Amplitude Modulation Enable",
                    "Amplitude Modulation", "am_enable",
                    &ym2612::OperatorSettings::amplitude_modulation_enable,
                    true);
  ImGui::Spacing();

  render_envelope(widget, envelope_state);

  if (column_layout) {
    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();
    ImGui::BeginGroup();
  } else {
    ImGui::Spacing();
  }

  // SSG Type Envelope Control (0-7)
  const int ssg_type = op.ssg_type_envelope_control;
  if (const auto *preview = op.ssg_enable ? get_ssg_preview_texture(ssg_type)
                                          : get_ssg_preview_off_texture()) {
    if (preview->valid()) {
      ImGui::Image(preview->texture_id, preview->size);
    }
  }
  ImGui::SameLine();

  operator_checkbox(widget, "SSG EG Enable", "SSG EG Enable", "ssg_enable",
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

  if (column_layout) {
    ImVec2 ssg_gap = ImGui::GetCursorPos();
    ImGui::SetCursorPosY(ssg_gap.y + 123);
  } else {
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
  operator_slider(
      widget, ym2612::OperatorField::Detune, "Detune", nullptr,
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
                            bool space_for_feedback) {
  auto &state = context.operator_edit;

  ImGui::PushID(slot);

  const ImVec2 frame_min = ImGui::GetCursorScreenPos();
  const float frame_width =
      std::max(ImGui::GetColumnWidth() - frame_gutter, 1.0f);

  ImGui::SetCursorScreenPos(
      ImVec2(frame_min.x + frame_padding, frame_min.y + frame_padding));
  ImGui::BeginGroup();
  render_operator_contents(context, patch, slot, envelope_state,
                           space_for_feedback);
  ImGui::EndGroup();

  const float content_height =
      ImGui::GetItemRectMax().y - frame_min.y + frame_padding;
  state.pending_frame_height =
      std::max(state.pending_frame_height, content_height);
  // Every operator is drawn to the tallest one's height so the four borders
  // line up. That height comes from the previous frame; measuring and
  // applying it in the same one would mean laying the section out twice.
  const float frame_height = std::max(content_height, state.frame_height);
  const ImVec2 frame_max(frame_min.x + frame_width,
                         frame_min.y + frame_height);
  // Claim the full frame so the columns row and the window's scroll extent
  // account for the padding and for any height borrowed from a taller
  // neighbour.
  ImGui::SetCursorScreenPos(ImVec2(frame_min.x, frame_max.y));

  // Hit testing by rectangle rather than by an invisible button underneath:
  // a button large enough to cover the operator would have to yield the
  // hover to every widget drawn on top of it, and IsAnyItemHovered already
  // says exactly that. Items submitted so far this frame are the ones that
  // could be under the cursor here, since the others are in other columns.
  const bool window_hovered =
      ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
  const bool frame_hovered =
      window_hovered && ImGui::IsMouseHoveringRect(frame_min, frame_max);
  const bool on_background = frame_hovered && !ImGui::IsAnyItemHovered();
  // The border only lights up on hover when nothing is being dragged, so a
  // slider drag that wanders out of its own operator does not light up the
  // one it passes over.
  const bool background_hovered = on_background && !ImGui::IsAnyItemActive();

  if (on_background && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    note_operator_click(state.selection, slot);
    state.click_claimed = true;
  }

  // Right-click opens the operator's menu wherever it lands inside the
  // frame, including on top of a slider, which has no use for the button.
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

  ImGui::GetWindowDrawList()->AddRect(
      frame_min, frame_max,
      operator_frame_color(state.selection.contains(slot),
                           state.selection.primary == slot,
                           background_hovered),
      ImGui::GetStyle().FrameRounding);

  ImGui::PopID();
}

} // namespace ui
