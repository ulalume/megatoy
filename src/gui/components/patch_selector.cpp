#include "patch_selector.hpp"

#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include "patch_selector_shared.hpp"
#include "patch_table_view.hpp"
#include "patch_tree_view.hpp"

#include <IconsFontAwesome7.h>
#include <imgui.h>
#include <string>

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

void render_add_folder_action(PatchSelectorContext &context) {
  const char *label = PreferenceManager::folder_add_is_import()
                          ? ICON_FA_FOLDER " Import Folder..."
                          : ICON_FA_FOLDER " Add Folder...";
  if (ImGui::Button(label)) {
    context.add_folder();
  }
  ImGui::Separator();
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
  }
  if (context.add_folder) {
    render_add_folder_action(context);
  }

  if (ImGui::BeginTabBar("##PatchViewMode")) {
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
