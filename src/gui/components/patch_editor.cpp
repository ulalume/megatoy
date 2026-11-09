#include "patch_editor.hpp"
#include "audio/audio_manager.hpp"
#include "gui/app_window.hpp"

#include "common.hpp"
#include "gui/areas.hpp"
#include "gui/components/instrument_icon_picker.hpp"
#include "gui/components/named_slider.hpp"
#include "gui/components/operator_commands.hpp"
#include "gui/components/preview/algorithm_preview.hpp"
#include "gui/fonts/megatoy_icons.hpp"
#include "gui/save_export_actions.hpp"
#include "gui/scroll_reveal.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/tab_bar_lines.hpp"
#include "gui/ui_scale.hpp"
#include "gui/window_title.hpp"
#include "operator_editor.hpp"
#include "patch_editor_tools.hpp"
#include "patches/instrument_icons.hpp"
#include "patches/save_controls.hpp"
#include "platform/platform_config.hpp"
#include "star_rating.hpp"
#include "view_patch_drop.hpp"
#include <algorithm>
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
    context.begin_history(label, key);
  }
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    context.commit_history();
  }
}

namespace {

#if defined(MEGATOY_PLATFORM_WEB)
/// Bottom-left of the item just drawn, where a menu it opens belongs.
ImVec2 menu_anchor_below_last_item() {
  return ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y);
}
#endif

/// The glyph of the patch's instrument icon, or the none icon.
const char *document_icon(const patches::PatchSession &session,
                          const patches::PatchDocument &document) {
  const auto *entry = session.document_entry(document);
  return patches::instrument_icons::glyph(
      entry != nullptr && entry->metadata
          ? std::string_view(entry->metadata->icon)
          : std::string_view());
}

/// What the header's metadata asks to write, once it is drawn.
struct MetadataEdit {
  std::optional<std::string> icon;
  std::optional<int> rating;
};

/**
 * The patch's instrument icon, large, at the start of the header. A button
 * that picks another for a patch the browser lists in a folder that keeps
 * metadata.
 */
void render_header_icon(const patches::PatchEntry *entry, bool editable,
                        float size, MetadataEdit &edit) {
  const std::string icon =
      entry != nullptr && entry->metadata ? entry->metadata->icon : "";
  if (!editable) {
    ImGui::Dummy(ImVec2(size, size));
    draw_instrument_icon(icon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                         kLargeIconScale);
    return;
  }
  const ImGuiID popup_id = ImGui::GetID("##icon_picker");
  // No fill at rest: the icon stands alone until the pointer is over it.
  ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
  if (ImGui::Button("##icon", ImVec2(size, size))) {
    ImGui::OpenPopupEx(popup_id);
  }
  ImGui::PopStyleColor();
  draw_instrument_icon(icon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                       kLargeIconScale);
  if (patches::instrument_icons::known(icon)) {
    ImGui::SetItemTooltip(
        "Icon: %s", patches::instrument_icons::display_name(icon).c_str());
  } else {
    ImGui::SetItemTooltip("Icon");
  }
  if (auto picked = instrument_icon_popup(popup_id, icon);
      picked && *picked != icon) {
    edit.icon = std::move(picked);
  }
}

/// The rating, written once let go as the browser's star filter is.
void render_header_rating(const patches::PatchEntry &entry,
                          UIState::DocumentUi &ui, MetadataEdit &edit) {
  int rating = ui.pending_rating.value_or(
      entry.metadata ? entry.metadata->star_rating : 0);
  if (star_rating_slider("##rating", rating, "Rating")) {
    ui.pending_rating = rating;
  }
  if (ImGui::IsItemDeactivated() && ui.pending_rating) {
    edit.rating = *ui.pending_rating;
    ui.pending_rating.reset();
  }
}

/// Save, and Save As... where it is offered, as one row.
void render_save_buttons(PatchEditorContext &context,
                         patches::PatchDocument &document,
                         const patches::SaveControls &controls,
                         PatchEditorState &state) {
  auto &patch_session = context.session;
  const bool save_button_is_disabled = !controls.save_enabled;

  if (save_button_is_disabled) {
    ImGui::BeginDisabled(true);
  }

  const char *save_label = controls.save_label;
  ImVec2 pos = ImGui::GetCursorPos();
  if (ImGui::Button(save_label)) {
    context.activate(document.id());
    if (controls.saves_in_place) {
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
  if (ImGui::IsItemHovered() && !controls.save_tooltip.empty()) {
    ImGui::SetTooltip("%s", controls.save_tooltip.c_str());
  }
  if (save_button_is_disabled) {
    ImGui::EndDisabled();
  }

  if (controls.show_save_as) {
    ImGui::SameLine();
    if (ImGui::Button("Save As...")) {
      context.activate(document.id());
      request_save_as(state);
#if defined(MEGATOY_PLATFORM_WEB)
      state.save_as_menu_anchor = menu_anchor_below_last_item();
#endif
    }
  }
}

/**
 * The header: the patch's icon; the save buttons, with the rating at the
 * row's right, over where the patch is kept, cut short where the window is
 * narrow. The rating goes below only when the buttons leave it no room.
 */
void render_document_header(PatchEditorContext &context,
                            patches::PatchDocument &document,
                            UIState::DocumentUi &ui, PatchEditorState &state,
                            bool is_active) {
  auto &patch_session = context.session;
  const ImGuiStyle &style = ImGui::GetStyle();
  const auto *entry = patch_session.document_entry(document);
  const bool editable =
      entry != nullptr && patch_session.repository().can_edit_metadata(*entry);

  // Two lines tall: the save buttons' and the path's.
  const float height = ImGui::GetFrameHeight() + style.ItemSpacing.y +
                       ImGui::GetTextLineHeight();
  const float top = ImGui::GetCursorPosY();
  MetadataEdit edit;
  render_header_icon(entry, editable, height, edit);

  ImGui::SameLine();
  const float left = ImGui::GetCursorPosX();
  const float right = left + ImGui::GetContentRegionAvail().x;
  const auto controls = patches::save_controls(patch_session, document);
  render_save_buttons(context, document, controls, state);
  const float buttons_end =
      ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetScrollX();
  const bool rating_beside =
      editable &&
      right - buttons_end - style.ItemSpacing.x >= star_rating_width();
  if (rating_beside) {
    ImGui::SetCursorPos(ImVec2(right - star_rating_width(), top));
    render_header_rating(*entry, ui, edit);
  }

  const auto &repository = patch_session.repository();
  const auto relative_path =
      repository.to_relative_path(document.path()).generic_string();
  const std::string path =
      display_preset_path(repository.display_path(relative_path));
  ImGui::SetCursorPos(
      ImVec2(left, top + ImGui::GetFrameHeight() + style.ItemSpacing.y));
  const ImVec2 path_min = ImGui::GetCursorScreenPos();
  const ImVec2 path_max(path_min.x + std::max(right - left, 0.0f),
                        path_min.y + ImGui::GetTextLineHeight());
  ImGui::PushStyleColor(ImGuiCol_Text,
                        styles::color(styles::MegatoyCol::TextMuted));
  ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), path_min, path_max,
                            path_max.x, path.c_str(), nullptr, nullptr);
  ImGui::PopStyleColor();
  ImGui::Dummy(ImVec2(path_max.x - path_min.x, ImGui::GetTextLineHeight()));

  if (editable && !rating_beside) {
    render_header_rating(*entry, ui, edit);
  }

  // Written once the header is drawn: a write rebuilds the browser's tree,
  // and `entry` with it.
  if (edit.icon) {
    patch_session.set_entry_icon(*entry, *edit.icon);
  } else if (edit.rating) {
    patch_session.set_entry_rating(*entry, *edit.rating);
  }

  // Render popups in the same window/ID stack as the actions that open them.
  // They act on the active document, so only its window holds them.
  if (is_active) {
    render_save_export_popups(patch_session, state);
  }
}

/// The LFO and Channel sections' sliders' names, in full or as AMS, FMS and
/// ALG.
struct LfoNames {
  const char *ams;
  const char *fms;
  const char *algorithm;
  bool short_names;
};
constexpr LfoNames kLfoFullNames{"Amplitude Modulation Sensitivity",
                                 "Frequency Modulation Sensitivity",
                                 "Algorithm", false};
constexpr LfoNames kLfoShortNames{"AMS", "FMS", "ALG", true};

/// The narrowest the algorithm diagram is drawn, where the names are short.
constexpr float kMinAlgorithmDiagramWidth = 112.0f;

/// The LFO section's sliders: one width for the two, wide enough for the
/// longer name.
float lfo_slider_width(const LfoNames &names) {
  return std::max(hslider_width(), named_slider_fit_width(names.fms, "0"));
}

void render_lfo_section(PatchEditorContext &context, ym2612::Patch &patch,
                        const LfoNames &names) {
  // The LFO's switch and rate are how the patch is played, set from the
  // menu bar; what the patch holds is how far it follows the LFO.
  ImGui::SeparatorText("LFO");

  // The sensitivities do nothing while the LFO is off, which is said by the
  // one being touched.
  // A short name is told in full there too.
  const bool lfo_on = context.session.playback().lfo_enable;
  const bool short_names = names.short_names;
  const auto tell = [&](const char *full_name) {
    const bool touched = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (!touched) {
      return;
    }
    constexpr const char *kOff =
        "The LFO is off. Turn it on from the LFO menu.";
    if (!lfo_on && short_names) {
      ImGui::SetTooltip("%s\n%s", full_name, kOff);
    } else if (!lfo_on) {
      ImGui::SetTooltip("%s", kOff);
    } else if (short_names && ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", full_name);
    }
  };

  ImGui::PushItemWidth(lfo_slider_width(names));

  int ams = patch.channel.amplitude_modulation_sensitivity;
  bool ams_changed = named_slider_int(names.ams, &ams, 0, 3);
  tell(kLfoFullNames.ams);
  track_patch_history(context, "Amplitude Modulation Sensitivity",
                      "channel.am_sensitivity");
  if (ams_changed) {
    patch.channel.amplitude_modulation_sensitivity = static_cast<uint8_t>(ams);
  }

  int fms = patch.channel.frequency_modulation_sensitivity;
  bool fms_changed = named_slider_int(names.fms, &fms, 0, 7);
  tell(kLfoFullNames.fms);
  track_patch_history(context, "Frequency Modulation Sensitivity",
                      "channel.fm_sensitivity");
  if (fms_changed) {
    patch.channel.frequency_modulation_sensitivity = static_cast<uint8_t>(fms);
  }

  ImGui::PopItemWidth();
  ImGui::Spacing();
}

void render_channel_section(PatchEditorContext &context, ym2612::Patch &patch,
                            const LfoNames &names) {
  ImGui::SeparatorText("Channel");
  ImGui::PushItemWidth(hslider_width());

  // The slider stands beside the diagram, as tall as it, at the spacing
  // between any two items. With the names short, the diagram narrows down
  // to its least to leave the slider its own.
  const float diagram_height = ui::scale::px(48.0f);
  float diagram_width = ImGui::CalcItemWidth();
  if (names.short_names) {
    diagram_width = std::clamp(
        ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x -
            named_slider_fit_width(names.algorithm, "0"),
        ui::scale::px(kMinAlgorithmDiagramWidth), diagram_width);
  }
  algorithm_diagram(patch.instrument.algorithm,
                    ImVec2(diagram_width, diagram_height));
  ImGui::SameLine();
  ImGui::PushStyleVarY(
      ImGuiStyleVar_FramePadding,
      std::max((diagram_height - ImGui::GetFontSize()) * 0.5f, 0.0f));

  int algorithm = patch.instrument.algorithm;
  ImGui::SetNextItemWidth(
      std::min(ImGui::CalcItemWidth(), ImGui::GetContentRegionAvail().x));
  bool algorithm_changed = named_slider_int(names.algorithm, &algorithm, 0, 7);
  if (names.short_names) {
    ImGui::SetItemTooltip("%s", kLfoFullNames.algorithm);
  }
  ImGui::PopStyleVar();
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
  const float column_min_width = ui::scale::px(250.0f);
  if (avail_width > column_min_width * 4) {
    ImGui::Columns(4, "operation_columns", false);
  } else if (avail_width > column_min_width * 2) {
    ImGui::Columns(2, "operation_columns", false);
  } else {
    ImGui::Columns(1, "operation_columns", false);
  }

  // The four frames share one height so their borders line up; each operator
  // reports what it needed and the tallest wins the next frame.
  context.operator_edit().pending_frame_height = 0.0f;
  context.operator_edit().click_claimed = false;
  echo_selection_highlights(context);
  for (int slot = 0; slot < 4; slot++) {
    render_operator_editor(context, patch, slot, context.envelope_state(slot),
                           voices);

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
            // Empty merge key: two pastes in a row stay two undo steps.
            context.begin_history(label, {});
          },
          [&context]() { context.commit_history(); }};
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
                     ICON_MEGATOY_TRIANGLE_EXCLAMATION
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

  const auto title =
      patch_editor_window_title(std::string(document_icon(session, document)) +
                                    " " + document_label(document),
                                ui.window_slot);
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
  const bool was_active = id == session.active_document_id();
  ImGui::PushStyleColor(ImGuiCol_WindowBg,
                        styles::color(styles::MegatoyCol::PatchEditorBg));
  // Marked unsaved, a document's tab stays in place when its close button
  // is pressed, while the user is asked about the changes.
  const bool visible = ui::begin_window(
      title.c_str(), session.can_close_document(id) ? &open : nullptr,
      ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoCollapse |
          (document.is_modified() ? ImGuiWindowFlags_UnsavedDocument : 0));
  ImGui::PopStyleColor();
  // A tab bar is dimmed while its node does not have the keys, which the
  // active document's often lacks: a click in the browser keeps them there.
  // Its bar stays lit regardless, so it can be told from the others.
  if (was_active) {
    keep_tab_bar_lit();
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
    render_document_header(context, document, ui, state, is_active);
    ImGui::Spacing();

    // Side by side while each of the two even columns holds the LFO's
    // sliders, named in full or else as AMS, FMS and ALG; one above the
    // other, in full, after that.
    // The Channel column needs the diagram and the Algorithm slider named
    // beside it, which is shortened with the LFO's.
    const auto available_width = ImGui::GetContentRegionAvail().x;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const auto fits_beside = [&](const LfoNames &names) {
      const float diagram_width =
          !names.short_names ? hslider_width()
                             : ui::scale::px(kMinAlgorithmDiagramWidth);
      const float channel_width = diagram_width + spacing +
                                  named_slider_fit_width(names.algorithm, "0");
      const float column =
          std::max(lfo_slider_width(names), channel_width) + spacing * 2.0f;
      return available_width >= column * 2.0f;
    };
    const bool full_beside = fits_beside(kLfoFullNames);
    const bool beside = full_beside || fits_beside(kLfoShortNames);
    ImGui::Columns(beside ? 2 : 1, "##lfo_channel_columns", false);
    const LfoNames &names =
        beside && !full_beside ? kLfoShortNames : kLfoFullNames;
    render_lfo_section(context, patch, names);
    ImGui::NextColumn();
    render_channel_section(context, patch, names);
    ImGui::Columns(1);
    // Only the active document is what the chip is playing.
    render_operator_section(context, patch,
                            is_active ? frame.voices : EnvelopeVoices{});
    render_patch_editor_tools(context, patch);
    handle_operator_deselect(context);
    handle_operator_shortcuts(context, patch);
    scroll_reveal::step();
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

  EditorFrame frame;
  // Read once, so all four graphs draw the same instant -- and so the audio
  // thread's publication is sampled once a frame rather than four times.
  frame.voices = collect_envelope_voices(context.audio.voice_activity());
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
