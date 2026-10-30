#include "patch_editor.hpp"
#include "common.hpp"
#include "gui/areas.hpp"
#include "gui/components/operator_commands.hpp"
#include "gui/components/preview/algorithm_preview.hpp"
#include "gui/save_export_actions.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include "gui/window_title.hpp"
#include "operator_editor.hpp"
#include "platform/platform_config.hpp"
#include "star_rating.hpp"
#include "view_patch_drop.hpp"
#include <IconsFontAwesome7.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <imgui.h>
#include <imgui_internal.h>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
/// Bottom-left of the item just drawn, where a menu it opens belongs.
ImVec2 menu_anchor_below_last_item() {
  return ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y);
}
#endif

/**
 * The rating and category of the patch's file, at the right of the row,
 * or below it when the row has no room. Only for a patch the browser lists
 * in a folder that keeps metadata.
 */
void render_patch_metadata(PatchEditorContext &context,
                           const patches::PatchDocument &document,
                           UIState::DocumentUi &ui) {
  auto &session = context.session;
  const auto &key = document.selection_path().empty()
                        ? document.path()
                        : document.selection_path();
  const auto *entry = session.find_entry(key);
  if (entry == nullptr || !session.repository().can_edit_metadata(*entry)) {
    return;
  }
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float category_width = ui::scale::px(110.0f);
  const float needed = category_width + spacing + star_rating_width();
  ImGui::SameLine();
  if (ImGui::GetContentRegionAvail().x >= needed) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         ImGui::GetContentRegionAvail().x - needed);
  } else {
    ImGui::NewLine();
  }

  const std::string saved_category =
      entry->metadata ? entry->metadata->category : std::string();
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%s",
                ui.pending_category.value_or(saved_category).c_str());
  ImGui::SetNextItemWidth(category_width);
  if (ImGui::InputTextWithHint("##category", "Category", buffer,
                               sizeof(buffer))) {
    ui.pending_category = std::string(buffer);
  }
  if (ImGui::IsItemDeactivated() && ui.pending_category) {
    const std::string category = std::move(*ui.pending_category);
    ui.pending_category.reset();
    // The tree is rebuilt by the write, so `entry` is not used after it.
    session.set_entry_category(*entry, category);
    return;
  }

  // Written once let go, as the browser's star filter is dragged the same.
  ImGui::SameLine(0.0f, spacing);
  int rating = ui.pending_rating.value_or(
      entry->metadata ? entry->metadata->star_rating : 0);
  if (star_rating_slider("##rating", rating, "Rating")) {
    ui.pending_rating = rating;
  }
  if (ImGui::IsItemDeactivated() && ui.pending_rating) {
    const int chosen = *ui.pending_rating;
    ui.pending_rating.reset();
    session.set_entry_rating(*entry, chosen);
  }
}

void render_save_export_buttons(PatchEditorContext &context,
                                patches::PatchDocument &document,
                                UIState::DocumentUi &ui,
                                PatchEditorState &state, bool is_active) {
  auto &patch_session = context.session;

  const bool is_user_patch = patch_session.is_user_patch(document);
  const bool is_patch_modified = document.is_modified();

  auto save_button_is_disabled = is_user_patch && !is_patch_modified;

  if (save_button_is_disabled) {
    ImGui::BeginDisabled(true);
  }

  const char *save_label =
      patch_session.save_label_for(document, is_user_patch);
  ImVec2 pos = ImGui::GetCursorPos();
  if (ImGui::Button(save_label)) {
    context.activate(document.id());
    if (is_user_patch) {
      trigger_save(patch_session, state);
    } else {
      request_save_as(state);
#if defined(MEGATOY_PLATFORM_WEB)
      state.save_as_menu_anchor = menu_anchor_below_last_item();
#endif
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
      const auto &path = document.path();
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
      context.activate(document.id());
      request_save_as(state);
#if defined(MEGATOY_PLATFORM_WEB)
      state.save_as_menu_anchor = menu_anchor_below_last_item();
#endif
    }
  }

  ImGui::SameLine();
  const auto &repository = patch_session.repository();
  const auto relative_path =
      repository.to_relative_path(document.path()).generic_string();
  ImGui::Text("%s",
              display_preset_path(repository.display_path(relative_path))
                  .c_str());
  render_patch_metadata(context, document, ui);

  // Render popups in the same window/ID stack as the actions that open them.
  // They act on the active document, so only its window holds them.
  if (is_active) {
    render_save_export_popups(patch_session, state);
  }
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

void render_operator_section(PatchEditorContext &context, ym2612::Patch &patch,
                             const EnvelopeVoices &voices) {

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
  context.operator_edit().pending_frame_height = 0.0f;
  context.operator_edit().click_claimed = false;
  for (int slot = 0; slot < 4; slot++) {
    render_operator_editor(context, patch, slot, context.envelope_state(slot),
                           space_for_feedbacks[slot], voices);

    ImGui::Spacing();
    ImGui::NextColumn();
  }
  context.operator_edit().frame_height =
      context.operator_edit().pending_frame_height;
  ImGui::Columns(1);
}

OperatorCommandContext make_operator_commands(PatchEditorContext &context,
                                              ym2612::Patch &patch) {
  return {patch.instrument, context.operator_edit(), context.operator_clipboard,
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
  auto &selection = context.operator_edit().selection;
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
  if (!context.operator_edit().click_claimed &&
      ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
      ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
      !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive()) {
    selection.clear();
  }
}

/// How a preview document's label is drawn on its tab and title bar, told
/// apart from a document that is kept open. Pushed before Begin(), which
/// records it for the tab, and popped right after.
void push_preview_label_style() {
  ImGui::PushStyleColor(
      ImGuiCol_Text,
      color_with_alpha_vec4(ImGui::GetStyleColorVec4(ImGuiCol_Text), 0.5f));
}

void pop_preview_label_style() { ImGui::PopStyleColor(); }

/// Select the current window's tab in its dock node, or raise it when it is
/// not docked, without taking the keyboard from another window. False while
/// the node has not built its tabs yet, as on the first frame.
bool bring_current_window_forward() {
  ImGuiWindow *window = ImGui::GetCurrentWindow();
  if (ImGuiDockNode *node = window->DockNode) {
    if (node->TabBar == nullptr) {
      return node->Windows.Size <= 1;
    }
    node->TabBar->NextSelectedTabId = window->TabId;
    return true;
  }
  if (window->DockId != 0) {
    return false;
  }
  ImGui::BringWindowToDisplayFront(window);
  return true;
}

bool is_patch_editor_window(const ImGuiWindow *window) {
  return window != nullptr &&
         std::string_view(window->Name)
                 .find(std::string("###") + PATCH_EDITOR_TITLE) !=
             std::string_view::npos;
}

/// Give every document a window slot, the lowest number no other open
/// document holds.
void assign_window_slots(PatchEditorContext &context) {
  std::vector<int> taken;
  for (const auto &document : context.session.documents()) {
    const int slot = context.document_ui(document->id()).window_slot;
    if (slot > 0) {
      taken.push_back(slot);
    }
  }
  for (const auto &document : context.session.documents()) {
    auto &ui = context.document_ui(document->id());
    if (ui.window_slot > 0) {
      continue;
    }
    int slot = 1;
    while (std::find(taken.begin(), taken.end(), slot) != taken.end()) {
      ++slot;
    }
    ui.window_slot = slot;
    taken.push_back(slot);
  }
}

/**
 * Across the top of a document whose file was written outside megatoy while
 * it had unsaved changes: take the file's contents, or keep the edits and
 * count the file's contents as what they differ from.
 */
void render_disk_change_bar(PatchEditorContext &context,
                            patches::DocumentId id,
                            UIState::DocumentUi &ui) {
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(styles::color(styles::MegatoyCol::TextHighlight),
                     ICON_FA_TRIANGLE_EXCLAMATION
                     " The file was changed outside megatoy.");
  ImGui::SameLine();
  if (ImGui::Button("Reload")) {
    if (context.reload_from_disk) {
      context.reload_from_disk(id, *ui.disk_version);
    }
    ui.disk_version.reset();
  }
  ImGui::SameLine();
  if (ImGui::Button("Keep Mine")) {
    context.session.accept_disk_version(id, *ui.disk_version,
                                        /*replace=*/false);
    ui.disk_version.reset();
  }
  ImGui::Separator();
}

struct EditorFrame {
  EnvelopeVoices voices;
  /// Some editor window had the keyboard as the frame began.
  bool editor_focused = false;
  /// The dock node of the window that had it.
  const ImGuiDockNode *focused_node = nullptr;
  /// The dock node of the first editor window drawn this frame.
  ImGuiID first_dock_id = 0;
  std::vector<patches::DocumentId> to_close;
};

void render_document_window(PatchEditorContext &context,
                            PatchEditorState &state,
                            patches::PatchDocument &document,
                            EditorFrame &frame) {
  const auto id = document.id();
  context.document = id;
  auto &ui = context.document_ui(id);
  auto &session = context.session;

  const auto title = patch_editor_window_title(
      document_label(document), ui.window_slot, document.is_modified());
  const ImGuiWindow *existing = ImGui::FindWindowByName(title.c_str());
  ImGuiDockNode *node = existing != nullptr ? existing->DockNode : nullptr;
  areas::keep_in_area(areas::Area::Main, title.c_str(), 0,
                      context.editor_dock_id);

  // A document dropped on a view goes into that view's dock node, and takes
  // the keys there.
  const ImGuiID dropped_on = std::exchange(ui.dock_target, 0u);
  ImGuiDockNode *drop_node =
      dropped_on != 0 ? ImGui::DockContextFindNodeByID(GImGui, dropped_on)
                      : nullptr;
  bool docking = false;
  if (drop_node != nullptr && ui.window_placed) {
    ImGui::SetNextWindowDockID(dropped_on, ImGuiCond_Always);
    node = drop_node;
    docking = true;
  }
  // A document's window opens beside the one the user was working in. The
  // slot may have been another document's before, so its scroll goes too.
  if (!ui.window_placed) {
    ui.window_placed = true;
    for (const ImGuiID dock_id :
         {drop_node != nullptr ? dropped_on : 0u,
          static_cast<ImGuiID>(context.editor_dock_id), frame.first_dock_id}) {
      if (dock_id == 0) {
        continue;
      }
      if (ImGuiDockNode *target =
              ImGui::DockContextFindNodeByID(GImGui, dock_id)) {
        ImGui::SetNextWindowDockID(dock_id, ImGuiCond_Always);
        node = target;
        docking = true;
        break;
      }
    }
    ImGui::SetNextWindowScroll(ImVec2(0.0f, 0.0f));
  }
  // A position given to a window that has none yet undocks it.
  if (!docking) {
    ImGui::SetNextWindowPos(ui::scale::px(ImVec2(400, 50)),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ui::scale::px(ImVec2(400, 600)),
                             ImGuiCond_FirstUseEver);
  }

  // Brought forward with the keyboard only when another editor window, or a
  // window sharing its dock node, has it -- a focused tab keeps its node
  // showing it. Otherwise whatever the user was typing into keeps the keys.
  const bool reveal = context.reveal_document == id;
  const bool take_focus =
      reveal && (drop_node != nullptr || frame.editor_focused ||
                 (node != nullptr && frame.focused_node == node));
  if (take_focus) {
    ImGui::SetNextWindowFocus();
  }

  const bool preview = !document.pinned();
  bool open = true;
  if (preview) {
    push_preview_label_style();
  }
  // ImGui dims a tab whose node does not have the keys, which the active
  // document's often lacks: a click in the browser keeps them there. Its tab
  // stays lit regardless, so it can be told from the others.
  const bool was_active = id == session.active_document_id();
  if (was_active) {
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected,
                          ImGui::GetStyleColorVec4(ImGuiCol_TabSelected));
    ImGui::PushStyleColor(
        ImGuiCol_TabDimmedSelectedOverline,
        ImGui::GetStyleColorVec4(ImGuiCol_TabSelectedOverline));
  }
  const bool visible = ImGui::Begin(title.c_str(), &open,
                                    ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_NoCollapse);
  if (was_active) {
    ImGui::PopStyleColor(2);
  }
  if (preview) {
    pop_preview_label_style();
  }
  // Right after Begin() the last item is the window's tab, or its title bar
  // when it is not docked: a double click there keeps the preview open.
  if (preview && ImGui::IsItemHovered() &&
      ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
    session.pin(id);
  }
  if (reveal && (take_focus || bring_current_window_forward())) {
    context.reveal_document.reset();
  }
  if (frame.first_dock_id == 0) {
    frame.first_dock_id = ImGui::GetWindowDockID();
  }

  // The window the user turns to is the active document: focusing it, or
  // clicking into it before the focus has moved.
  const bool focused =
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  const bool clicked = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
                       (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                        ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                        ImGui::IsMouseClicked(ImGuiMouseButton_Middle));
  if ((focused && !ui.window_focused) || clicked) {
    context.activate(id);
  }
  ui.window_focused = focused;

  const bool is_active = id == session.active_document_id();
  if (is_active) {
    context.editor_dock_id = ImGui::GetWindowDockID();
  }

  if (visible) {
    auto &patch = document.patch();
    // Keyed by document, so a window slot that showed another document
    // keeps nothing of it.
    ImGui::PushID(static_cast<int>(id));
    if (ui.disk_version) {
      render_disk_change_bar(context, id, ui);
    }
    render_save_export_buttons(context, document, ui, state, is_active);
    ImGui::Spacing();

    const auto available_width = ImGui::GetContentRegionAvail().x;
    ImGui::Columns(available_width > ui::scale::px(800.0f) ? 2 : 1,
                   "##lfo_channel_columns", false);
    render_lfo_section(context, patch);
    ImGui::NextColumn();
    render_channel_section(context, patch);
    ImGui::Columns(1);
    // Only the active document is what the chip is playing.
    render_operator_section(context, patch,
                            is_active ? frame.voices : EnvelopeVoices{});
    handle_operator_deselect(context);
    handle_operator_shortcuts(context, patch);
    ImGui::PopID();
  }
  view_patch_drop_target();
  ImGui::End();

  if (!open) {
    frame.to_close.push_back(id);
  }
}

} // namespace

bool patch_editor_has_focus() {
  const ImGuiWindow *focused = GImGui->NavWindow;
  return focused != nullptr && is_patch_editor_window(focused->RootWindow);
}

void render_patch_editors(PatchEditorContext &context,
                          PatchEditorState &state) {
  auto &session = context.session;
  assign_window_slots(context);

  // The selection belongs to the patch it was made in, so loading another
  // one -- or undoing a load back to the previous one -- drops it. Patch Lab
  // results keep it: they replace the contents of the same slot, and the
  // user is still working on the operators they picked. The clipboard is
  // deliberately untouched, so an operator can be carried across.
  for (const auto &document : session.documents()) {
    auto &edit = context.document_ui(document->id()).operator_edit;
    if (edit.selection_patch_path != document->selection_path()) {
      edit.selection_patch_path = document->selection_path();
      edit.selection.clear();
      edit.baseline.clear();
    }
  }

  if (!context.prefs.show_patch_editor) {
    return;
  }

  EditorFrame frame;
  // Read once, so all four graphs draw the same instant -- and so the audio
  // thread's publication is sampled once a frame rather than four times.
  frame.voices = collect_envelope_voices(session.voice_activity());
  frame.editor_focused = patch_editor_has_focus();
  if (const ImGuiWindow *focused = GImGui->NavWindow) {
    frame.focused_node = focused->RootWindow->DockNode;
  }

  std::vector<patches::DocumentId> ids;
  for (const auto &document : session.documents()) {
    ids.push_back(document->id());
  }
  for (const auto id : ids) {
    if (auto *document = session.find_document(id)) {
      render_document_window(context, state, *document, frame);
    }
  }
  for (const auto id : frame.to_close) {
    context.request_close(id);
  }
}

} // namespace ui
