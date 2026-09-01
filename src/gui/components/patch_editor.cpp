#include "patch_editor.hpp"
#include "common.hpp"
#include "core/status.hpp"
#include "gui/components/operator_commands.hpp"
#include "gui/components/preview/algorithm_preview.hpp"
#include "gui/save_export_actions.hpp"
#include "gui/ui_scale.hpp"
#include "gui/window_title.hpp"
#include "operator_editor.hpp"
#include "platform/platform_config.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include "platform/web/web_workspace_download.hpp"
#endif
#include <filesystem>
#include <imgui.h>
#include <optional>

namespace ui {

void track_patch_history(PatchEditorContext &context, const std::string &label,
                         const std::string &merge_key) {
  const std::string key = merge_key.empty() ? label : merge_key;
  if (ImGui::IsItemActivated()) {
    auto before = context.session.current_patch();
    if (context.begin_history) {
      context.begin_history(label, key, before);
    }
  }
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    if (context.commit_history) {
      context.commit_history();
    }
  }
}

namespace {

#if defined(MEGATOY_PLATFORM_WEB)
/**
 * The Download button's format menu.
 *
 * The same list Save As offers, named the same way, so the browser can take
 * the patch out in any format megatoy can write.
 */
void render_download_menu(patches::PatchSession &session) {
  if (!ImGui::BeginPopup("Download")) {
    return;
  }
  for (const auto &format : session.save_formats()) {
    if (ImGui::MenuItem(format.display_name().c_str())) {
      if (platform::web::download_patch(session.current_patch(),
                                        format.extension)) {
        megatoy::status::success("Download started.");
      } else {
        megatoy::status::error("Failed to prepare " + format.extension +
                               " download.");
      }
    }
  }
  ImGui::EndPopup();
}
#endif

void render_save_export_buttons(PatchEditorContext &context,
                                PatchEditorState &state) {
  auto &patch_session = context.session;

  auto is_user_patch = patch_session.current_patch_is_user_patch();
  auto is_patch_modified = patch_session.is_modified();

  auto save_button_is_disabled = is_user_patch && !is_patch_modified;

  if (save_button_is_disabled) {
    ImGui::BeginDisabled(true);
  }

  const char *save_label = save_label_for(patch_session, is_user_patch);
  ImVec2 pos = ImGui::GetCursorPos();
  if (ImGui::Button(save_label)) {
    if (is_user_patch) {
      trigger_save(patch_session, state);
    } else {
      request_save_as(state);
    }
  }

  // for hover
  if (save_button_is_disabled) {
    ImGui::SetCursorPos(pos);
    ImGui::EndDisabled();
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
    std::string dummy_label = std::string(save_label) + "##dummy";
    ImGui::Button(dummy_label.c_str());
    ImGui::PopStyleVar();
    ImGui::BeginDisabled(true);
  }
  if (ImGui::IsItemHovered()) {
    if (!is_user_patch) {
      const auto &path = patch_session.current_patch_path();
      if (path.ends_with(".ginpkg")) {
        ImGui::SetTooltip("Save version to %s", path.c_str());
      } else {
        ImGui::SetTooltip("Choose a filename and format");
      }
    } else if (!is_patch_modified) {
      ImGui::SetTooltip("Patch is not modified");
    }
  }
  if (save_button_is_disabled) {
    ImGui::EndDisabled();
  }

  if (is_user_patch) {
    ImGui::SameLine();
    if (ImGui::Button("Save As...")) {
      request_save_as(state);
    }
  }

#if defined(MEGATOY_PLATFORM_WEB)
  ImGui::SameLine();
  if (ImGui::Button("Download")) {
    ImGui::OpenPopup("Download");
  }
  render_download_menu(patch_session);
#endif

  ImGui::SameLine();
  auto relative_path = patch_session.repository().to_relative_path(
      patch_session.current_patch_path());
  ImGui::Text("%s", display_preset_path(relative_path).c_str());

  // Render popups in the same window/ID stack as the actions that open them.
  render_save_export_popups(patch_session, state, context.text_prompt_state);
}

void render_patch_metadata(PatchEditorContext &context,
                           PatchEditorState &state) {
  render_save_export_buttons(context, state);
  ImGui::Spacing();
}

void render_lfo_section(PatchEditorContext &context, ym2612::Patch &patch) {
  ImGui::SeparatorText("Low Frequency Oscillator");
  bool lfo_enable = patch.global.lfo_enable;
  if (ImGui::Checkbox("LFO Enable", &lfo_enable)) {
    track_instant_patch_history(context, "LFO Enable",
                                [&] { patch.global.lfo_enable = lfo_enable; });
  }

  ImGui::PushItemWidth(hslider_width());

  int lfo_freq = patch.global.lfo_frequency;

  if (!lfo_enable)
    ImGui::BeginDisabled(true);
  bool lfo_freq_changed = ImGui::SliderInt("LFO Frequency", &lfo_freq, 0, 7);

  track_patch_history(context, "LFO Frequency", "global.lfo_frequency");
  if (lfo_freq_changed) {
    patch.global.lfo_frequency = static_cast<uint8_t>(lfo_freq);
  }

  ImGui::Spacing();

  int ams = patch.channel.amplitude_modulation_sensitivity;
  bool ams_changed =
      ImGui::SliderInt("Amplitude Modulation Sensitivity", &ams, 0, 3);
  track_patch_history(context, "Amplitude Modulation Sensitivity",
                      "channel.am_sensitivity");
  if (ams_changed) {
    patch.channel.amplitude_modulation_sensitivity = static_cast<uint8_t>(ams);
  }

  int fms = patch.channel.frequency_modulation_sensitivity;
  bool fms_changed =
      ImGui::SliderInt("Frequency Modulation Sensitivity", &fms, 0, 7);
  track_patch_history(context, "Frequency Modulation Sensitivity",
                      "channel.fm_sensitivity");
  if (fms_changed) {
    patch.channel.frequency_modulation_sensitivity = static_cast<uint8_t>(fms);
  }
  if (!lfo_enable)
    ImGui::EndDisabled();

  ImGui::PopItemWidth();
  ImGui::Spacing();
}

void render_channel_section(PatchEditorContext &context, ym2612::Patch &patch) {
  ImGui::SeparatorText("Channel");
  bool left_speaker = patch.channel.left_speaker;
  if (ImGui::Checkbox("Left Speaker", &left_speaker)) {
    track_instant_patch_history(context, "Left Speaker", [&] {
      patch.channel.left_speaker = left_speaker;
    });
  }

  ImGui::SameLine();

  bool right_speaker = patch.channel.right_speaker;
  if (ImGui::Checkbox("Right Speaker", &right_speaker)) {
    track_instant_patch_history(context, "Right Speaker", [&] {
      patch.channel.right_speaker = right_speaker;
    });
  }

  ImGui::PushItemWidth(hslider_width());

  if (const auto *preview =
          get_algorithm_preview_texture(patch.instrument.algorithm)) {
    ImGui::Image(preview->texture_id, ui::scale::px(preview->size));
  }

  int algorithm = patch.instrument.algorithm;
  bool algorithm_changed = ImGui::SliderInt("Algorithm", &algorithm, 0, 7);
  track_patch_history(context, "Algorithm", "instrument.algorithm");
  if (algorithm_changed) {
    patch.instrument.algorithm = static_cast<uint8_t>(algorithm);
  }
  ImGui::PopItemWidth();

  ImGui::Spacing();
}

void render_operator_section(PatchEditorContext &context,
                             ym2612::Patch &patch) {

  const auto avail_width = ImGui::GetContentRegionAvail().x;
  bool space_for_feedbacks[4] = {false};
  const float column_min_width = ui::scale::px(250.0f);
  if (avail_width > column_min_width * 4) {
    ImGui::Columns(4, "operation_columns", false);
    space_for_feedbacks[0] = true;
    space_for_feedbacks[1] = true;
    space_for_feedbacks[2] = true;
    space_for_feedbacks[3] = true;
  } else if (avail_width > column_min_width * 2) {
    ImGui::Columns(2, "operation_columns", false);
    space_for_feedbacks[0] = true;
    space_for_feedbacks[1] = true;
  } else {
    ImGui::Columns(1, "operation_columns", false);
  }

  // The four frames share one height so their borders line up; each operator
  // reports what it needed and the tallest wins the next frame.
  context.operator_edit.pending_frame_height = 0.0f;
  context.operator_edit.click_claimed = false;
  // Read once, so all four graphs draw the same instant -- and so the audio
  // thread's publication is sampled once a frame rather than four times.
  const EnvelopeVoices voices =
      collect_envelope_voices(context.session.voice_activity());
  for (int slot = 0; slot < 4; slot++) {
    render_operator_editor(context, patch, slot, context.envelope_states[slot],
                           space_for_feedbacks[slot], voices);

    ImGui::Spacing();
    ImGui::NextColumn();
  }
  context.operator_edit.frame_height =
      context.operator_edit.pending_frame_height;
  ImGui::Columns(1);
}

OperatorCommandContext make_operator_commands(PatchEditorContext &context,
                                              ym2612::Patch &patch) {
  return {patch.instrument, context.operator_edit,
          [&context](const std::string &label) {
            if (context.begin_history) {
              // Empty merge key: two pastes in a row stay two undo steps.
              context.begin_history(label, {}, context.session.current_patch());
            }
          },
          [&context]() {
            if (context.commit_history) {
              context.commit_history();
            }
          }};
}

/**
 * Copy, paste and swap while the patch editor has the keyboard.
 *
 * Gated on this window rather than handled globally: Ctrl+C in the patch
 * browser belongs to the browser, and WantTextInput keeps it out of the
 * search box and every other text field.
 */
void handle_operator_shortcuts(PatchEditorContext &context,
                               ym2612::Patch &patch) {
  const ImGuiIO &io = ImGui::GetIO();
  if (io.WantTextInput ||
      !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    return;
  }
  if (!io.KeyCtrl && !io.KeySuper) {
    return;
  }

  auto commands = make_operator_commands(context, patch);
  if (io.KeyShift) {
    if (ImGui::IsKeyPressed(ImGuiKey_X, false)) {
      swap_operators_command(commands);
    }
    return;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_C, false)) {
    copy_operator_command(commands);
  } else if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
    paste_operator_command(commands);
  }
}

/**
 * Escape, or a click that landed on nothing, drops the selection.
 *
 * Called after the operators are drawn so IsAnyItemHovered() already
 * accounts for them: a click inside an operator is a click on something.
 */
void handle_operator_deselect(PatchEditorContext &context) {
  auto &selection = context.operator_edit.selection;
  if (selection.empty()) {
    return;
  }

  const ImGuiIO &io = ImGui::GetIO();
  if (!io.WantTextInput &&
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    selection.clear();
    return;
  }

  // An operator's own background is not "nothing": the click that selected
  // it looks exactly like a click on the window behind it, so the operators
  // say when one was theirs.
  if (!context.operator_edit.click_claimed &&
      ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
      ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
      !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive()) {
    selection.clear();
  }
}

} // namespace

// Function to render instrument settings panel
void render_patch_editor(const char *title, PatchEditorContext &context,
                         PatchEditorState &state) {
  auto &patch = context.session.current_patch();
  auto is_modified = context.session.is_modified();

  // The selection belongs to the patch it was made in, so loading another
  // one -- or undoing a load back to the previous one -- drops it. Patch Lab
  // results keep it: they replace the contents of the same slot, and the
  // user is still working on the operators they picked. The clipboard is
  // deliberately untouched, so an operator can be carried across.
  const auto &selection_path = context.session.current_patch_selection_path();
  if (context.operator_edit.selection_patch_path != selection_path) {
    context.operator_edit.selection_patch_path = selection_path;
    context.operator_edit.selection.clear();
    context.operator_edit.baseline.clear();
  }

  if (!context.prefs.show_patch_editor) {
    return;
  }

  ImGui::SetNextWindowPos(ui::scale::px(ImVec2(400, 50)),
                          ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ui::scale::px(ImVec2(400, 600)),
                           ImGuiCond_FirstUseEver);

  auto title_with_id = patch_editor_window_title(
      title, context.session.current_patch_path(), is_modified);
  // Match the waveform panel: no tab bar while this window is docked alone.
  ImGuiWindowClass window_class;
  window_class.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_AutoHideTabBar;
  ImGui::SetNextWindowClass(&window_class);
  if (ImGui::Begin(title_with_id.c_str(), &context.prefs.show_patch_editor)) {
    render_patch_metadata(context, state);

    const auto available_width = ImGui::GetContentRegionAvail().x;
    ImGui::Columns(available_width > ui::scale::px(800.0f) ? 2 : 1,
                   "##lfo_channel_columns", false);
    render_lfo_section(context, patch);
    ImGui::NextColumn();
    render_channel_section(context, patch);
    ImGui::Columns(1);
    render_operator_section(context, patch);
    handle_operator_deselect(context);
    handle_operator_shortcuts(context, patch);
  }

  ImGui::End();
  context.session.apply_patch_to_audio_if_changed();
}

} // namespace ui
