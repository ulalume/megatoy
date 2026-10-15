#include "patch_selector.hpp"

#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include "patch_selector_shared.hpp"
#include "patch_table_view.hpp"
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

/// The same action as an icon at the right end of the view tabs' row. Drawn
/// before the tabs, which are kept clear of it; returns the width it takes.
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

void render_tree_tab(PatchSelectorContext &context) {
  render_filter_bar(context);
  if (ImGui::BeginChild("PresetTree", ImGui::GetContentRegionAvail(), true)) {
    const std::string query_lower =
        to_lower(context.prefs.metadata_search_query);
    const bool rendered =
        render_patch_tree(context.repository.tree(), context, query_lower,
                          context.prefs.metadata_star_filter);
    if (!rendered &&
        (!query_lower.empty() || context.prefs.metadata_star_filter > 0)) {
      ImGui::TextColored(styles::color(styles::MegatoyCol::TextMuted),
                         "No results for current filters");
    }
  }
  ImGui::EndChild();
}

} // namespace

void render_patch_selector(const char *title, PatchSelectorContext &context) {
  auto &prefs = context.prefs;
  if (!prefs.show_patch_selector) {
    return;
  }

  ImGui::SetNextWindowPos(ui::scale::px(ImVec2(50, 400)),
                          ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ui::scale::px(ImVec2(350, 500)),
                           ImGuiCond_FirstUseEver);

  // Match the waveform panel: no tab bar while this window is docked alone.
  ImGuiWindowClass window_class;
  window_class.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_AutoHideTabBar;
  ImGui::SetNextWindowClass(&window_class);
  if (!ImGui::Begin(title, &prefs.show_patch_selector)) {
    ImGui::End();
    return;
  }

  if (context.repository.has_directory_changed()) {
    context.repository.refresh();
  }

  if (context.workspace_is_empty && context.add_folder) {
    render_empty_workspace_prompt(context);
    render_add_folder_button(context);
  }

  // The tab bar spans the window's work rect as it begins; narrowing that for
  // the call keeps the tabs off the icon and leaves the contents full width.
  ImGuiWindow *window = ImGui::GetCurrentWindow();
  const float work_right = window->WorkRect.Max.x;
  if (context.add_folder) {
    window->WorkRect.Max.x -= render_add_folder_icon(context);
  }
  const bool tabs = ImGui::BeginTabBar("##PatchViewMode");
  window->WorkRect.Max.x = work_right;
  if (tabs) {
    if (ImGui::BeginTabItem(ICON_FA_FOLDER_TREE " Tree view")) {
      render_tree_tab(context);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(ICON_FA_TABLE " Table view")) {
      render_filter_bar(context);
      render_patch_table(context);
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

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

  if (context.pending_menu_action) {
    const auto action = *context.pending_menu_action;
    context.pending_menu_action.reset();
    if (action == PendingMenuAction::Refresh) {
      context.repository.refresh();
    } else if (context.save_current_patch) {
      context.save_current_patch();
    }
  }

  ImGui::End();
}

} // namespace ui
