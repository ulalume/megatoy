#include "patch_selector.hpp"
#include "gui/app_window.hpp"

#include "gui/areas.hpp"
#include "gui/layouts.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include "gui/window_title.hpp"
#include "patch_selector_shared.hpp"
#include "patch_tree_view.hpp"

#include <IconsFontAwesome7.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <utility>

namespace ui {

namespace {

using namespace selector_detail;

void render_empty_workspace_prompt(PatchSelectorContext &context) {
  if (PreferenceManager::folder_add_is_import()) {
    ImGui::TextWrapped("Import a local folder of patches -- "
                       "megatoy copies it into browser storage.");
  } else {
    ImGui::TextWrapped("No patch folders yet.");
  }
}

const char *add_folder_label() {
  return PreferenceManager::folder_add_is_import() ? "Import Folder..."
                                                   : "Add Folder...";
}

/// The labelled button, under the prompt shown while there are no folders.
void render_add_folder_button(PatchSelectorContext &context) {
  const std::string label = std::string(ICON_FA_FOLDER_PLUS " ") +
                            add_folder_label();
  if (ImGui::Button(label.c_str())) {
    context.add_folder();
  }
  ImGui::Separator();
}

/// The same action as an icon at the right end of the filter row. Drawn
/// before the row, which is kept clear of it; returns the width it takes.
float render_add_folder_icon(PatchSelectorContext &context) {
  const ImVec2 row = ImGui::GetCursorScreenPos();
  const float size = ImGui::GetFrameHeight();
  const float right = ImGui::GetCurrentWindow()->WorkRect.Max.x;
  ImGui::SetCursorScreenPos(ImVec2(right - size, row.y));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  if (ImGui::Button(ICON_FA_FOLDER_PLUS "##add_folder", ImVec2(size, size))) {
    context.add_folder();
  }
  ImGui::PopStyleColor();
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", add_folder_label());
  }
  ImGui::SetCursorScreenPos(row);
  return size + ImGui::GetStyle().ItemSpacing.x;
}

void render_tree(PatchSelectorContext &context) {
  // Scrolls on its own, so the filter row above stays in place. It reaches
  // the window's edges; its own padding keeps the rows where they were.
  ImGuiWindow *window = ImGui::GetCurrentWindow();
  const ImRect inner = window->InnerRect;
  const float top = ImGui::GetCursorScreenPos().y;
  ImGui::SetCursorScreenPos(ImVec2(inner.Min.x, top));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(ImGui::GetStyle().WindowPadding.x, 0.0f));
  const bool visible = ImGui::BeginChild(
      "PresetTree", ImVec2(inner.GetWidth(), inner.Max.y - top),
      ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PopStyleVar();
  if (visible) {
    // Folder and file rows alike run to the area's edge: a file row's
    // highlight would otherwise stop half a spacing past a folder row's.
    ImGuiWindow *tree = ImGui::GetCurrentWindow();
    tree->WorkRect.Max.x = tree->InnerRect.Max.x;
    ImGui::PushClipRect(tree->InnerRect.Min, tree->InnerRect.Max, false);
    const std::string query_lower =
        patches::search_key(context.prefs.metadata_search_query);
    const bool rendered =
        render_patch_tree(context.repository.tree(), context, query_lower,
                          context.prefs.metadata_star_filter);
    ImGui::PopClipRect();
    if (!rendered &&
        (!query_lower.empty() || context.prefs.metadata_star_filter > 0)) {
      ImGui::TextColored(styles::color(styles::MegatoyCol::TextMuted),
                         "No results for current filters");
    }
  }
  ImGui::EndChild();
}

/// Everything inside the patch browser, whether a window or folded away.
void render_patch_selector_contents(PatchSelectorContext &context) {
  if (context.workspace_is_empty && context.add_folder) {
    render_empty_workspace_prompt(context);
    render_add_folder_button(context);
  }

  // The add-folder icon closes the filter row, which is kept clear of it.
  ImGuiWindow *window = ImGui::GetCurrentWindow();
  const float work_right = window->WorkRect.Max.x;
  if (context.add_folder) {
    window->WorkRect.Max.x -= render_add_folder_icon(context);
  }
  render_filter_bar(context);
  window->WorkRect.Max.x = work_right;
  render_tree(context);

  if (context.pending_remove_folder && context.remove_folder) {
    const auto folder = *context.pending_remove_folder;
    context.pending_remove_folder.reset();
    context.remove_folder(folder);
  }

  if (context.pending_move && context.move_patch) {
    const auto move = std::move(*context.pending_move);
    context.pending_move.reset();
    if (move.entry.is_directory) {
      follow_tree_directory(move.entry.relative_path,
                            move.destination.relative_path + "/" +
                                move.entry.full_path.filename().string());
    }
    context.move_patch(move.entry, move.destination);
  }

  if (context.pending_rating) {
    const auto rating = std::move(*context.pending_rating);
    context.pending_rating.reset();
    if (const auto *entry = context.session.find_entry(rating.relative_path)) {
      context.session.set_entry_rating(*entry, rating.stars);
    }
  }

  if (context.pending_menu_action) {
    context.pending_menu_action.reset();
    if (context.save_current_patch) {
      context.save_current_patch();
    }
  }
}

/**
 * The patch browser folded into a button at the left of the editors' tabs.
 * It opens the browser as a menu does, over the editor at its least width,
 * and leaves it open while patches are chosen so each shows behind it; a
 * click outside closes it.
 */
void render_folded_patch_selector(PatchSelectorContext &context) {
  static const ImGuiID popup_id = ImHashStr("##folded_patch_browser");
  // Whether the browser was open when the button was pressed: that press
  // closed it, as a press outside it does, and the release leaves it so.
  static bool open_at_press = false;

  ImGuiDockNode *editors =
      ImGui::DockBuilderGetCentralNode(ImGui::GetMainViewport()->ID);
  if (editors == nullptr || !ImGui::DockNodeBeginAmendTabBar(editors)) {
    return;
  }
  static const std::string label =
      std::string(PATCH_BROWSER_ICON) + "##folded_patch_browser";
  // Drawn as a tab button, but a button added to a dock's tab bar does not
  // take clicks itself, so the press and release are read here.
  ImGui::TabItemButton(label.c_str(), ImGuiTabItemFlags_Leading |
                                          ImGuiTabItemFlags_NoTooltip);
  const bool hovered = ImGui::IsItemHovered();
  // Also through the open browser, so a press here while it is open is
  // known for the one that closes it.
  const bool under_pointer =
      ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
  const ImVec2 below(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y);
  ImGui::DockNodeEndAmendTabBar();
  if (hovered) {
    ImGui::SetTooltip("%s", PATCH_BROWSER_TITLE);
  }
  if (under_pointer) {
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      open_at_press = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);
    }
    // On release, so the press is not taken for one outside the browser.
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !open_at_press) {
      ImGui::OpenPopupEx(popup_id);
    }
  }

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const float bottom = viewport->WorkPos.y + viewport->WorkSize.y;
  ImGui::SetNextWindowPos(below);
  ImGui::SetNextWindowSize(ImVec2(layouts::patch_browser_min_width(),
                                  ImMax(0.0f, bottom - below.y)));
  if (!ImGui::BeginPopupEx(popup_id,
                           ImGuiWindowFlags_NoTitleBar |
                               ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoResize |
                               ImGuiWindowFlags_NoSavedSettings |
                               ImGuiWindowFlags_NoScrollbar |
                               ImGuiWindowFlags_NoScrollWithMouse)) {
    return;
  }
  ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
  render_patch_selector_contents(context);
  ImGui::PopItemFlag();
  ImGui::EndPopup();
}

} // namespace

void render_patch_selector(const char *title, PatchSelectorContext &context) {
  auto &prefs = context.prefs;
  if (!prefs.show_patch_selector) {
    return;
  }
  if (layouts::patch_browser_folded()) {
    render_folded_patch_selector(context);
    return;
  }

  ImGui::SetNextWindowPos(ui::scale::px(ImVec2(50, 400)),
                          ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ui::scale::px(ImVec2(350, 500)),
                           ImGuiCond_FirstUseEver);

  // Match the waveform panel: no tab bar while this window is docked alone.
  areas::keep_in_area(areas::Area::Sidebar, title,
                      ImGuiDockNodeFlags_AutoHideTabBar);
  // Only the tree scrolls; it runs to the bottom edge.
  if (!ui::begin_window(title, &prefs.show_patch_selector,
                        ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoScrollWithMouse)) {
    ImGui::End();
    return;
  }
  render_patch_selector_contents(context);
  ImGui::End();
}

} // namespace ui
