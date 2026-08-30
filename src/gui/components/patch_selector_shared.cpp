#include "patch_selector_shared.hpp"

#include "common.hpp"
#include "file_manager.hpp"
#include "gui/ui_scale.hpp"
#include "platform/platform_config.hpp"

#include <IconsFontAwesome7.h>
#include <algorithm>
#include <cstring>
#include <imgui.h>

namespace ui::selector_detail {

const std::array<std::string_view, 6> kStarLabels = {
    "-",
    ICON_FA_STAR,
    ICON_FA_STAR ICON_FA_STAR,
    ICON_FA_STAR ICON_FA_STAR ICON_FA_STAR,
    ICON_FA_STAR ICON_FA_STAR ICON_FA_STAR ICON_FA_STAR,
    ICON_FA_STAR ICON_FA_STAR ICON_FA_STAR ICON_FA_STAR ICON_FA_STAR,
};
const std::array<std::string_view, 6> kStarLabelsMini = {
    "-",
    ICON_FA_STAR "1",
    ICON_FA_STAR "2",
    ICON_FA_STAR "3",
    ICON_FA_STAR "4",
    ICON_FA_STAR "5",
};

void show_patch_tooltip(const patches::PatchEntry &entry) {
  if (!ImGui::IsItemHovered()) {
    return;
  }

  std::string tooltip = "Format: " + entry.format +
                        "\nPath: " + display_preset_path(entry.relative_path);

  if (entry.metadata) {
    tooltip += "\nStars: " + std::to_string(entry.metadata->star_rating) + "/5";
    if (!entry.metadata->category.empty()) {
      tooltip += "\nCategory: " + entry.metadata->category;
    }
    if (!entry.metadata->notes.empty()) {
      tooltip += "\nNotes: " + entry.metadata->notes;
    }
  }

  ImGui::SetTooltip("%s", tooltip.c_str());
}

void entry_context_menu(PatchSelectorContext &context,
                        const patches::PatchEntry &entry,
                        bool allow_remove_folder) {
  if (!ImGui::BeginPopupContextItem(nullptr)) {
    return;
  }

  const bool is_current =
      !entry.is_directory &&
      entry.relative_path == context.session.current_patch_selection_path();

  const bool can_create_patch =
      entry.is_directory && context.create_patch_in &&
      context.session.can_create_patch_in(entry.full_path);
  if (can_create_patch && ImGui::MenuItem("New Patch...")) {
    context.create_patch_in(entry.full_path);
  }

  if (!entry.is_directory && !is_current && context.safe_load_patch) {
    if (ImGui::MenuItem("Open")) {
      context.safe_load_patch(entry);
    }
  }

  if (is_current) {
    const bool can_primary_save = context.session.current_patch_is_user_patch();
    if (can_primary_save && context.save_current_patch) {
      const bool disabled = !context.session.is_modified();
      ImGui::BeginDisabled(disabled);
      if (ImGui::MenuItem(context.session.save_label_for(true))) {
        context.pending_menu_action = PendingMenuAction::SaveCurrent;
      }
      ImGui::EndDisabled();
    }
    if (context.save_current_patch_as && ImGui::MenuItem("Save As...")) {
      context.save_current_patch_as();
    }
  }

  if (context.download_entry) {
    if (ImGui::MenuItem("Download")) {
      context.download_entry(entry);
    }
  }

  if (can_create_patch || (!entry.is_directory && !is_current) || is_current ||
      context.download_entry) {
    ImGui::Separator();
  }

  // Whether a file manager exists is the composition root's call -- the
  // callback is simply absent on platforms without one.
  if (context.reveal_in_file_manager) {
    if (ImGui::MenuItem(ui::reveal_in_file_manager_label())) {
      context.reveal_in_file_manager(
          context.repository.to_absolute_path(entry.relative_path));
    }
    ImGui::Separator();
  }

  const bool protected_folder = context.folder_is_protected &&
                                context.folder_is_protected(entry.full_path);

  const bool can_rename =
      context.repository.can_rename_patch(entry) && !protected_folder;
  const bool can_delete = context.repository.can_delete_patch(entry);
  if (can_rename || can_delete) {
    if (can_rename && context.rename_patch && ImGui::MenuItem("Rename...")) {
      context.rename_patch(entry);
    }
    if (can_delete && context.delete_patch && ImGui::MenuItem("Delete...")) {
      context.delete_patch(entry);
    }
    ImGui::Separator();
  }

  if (allow_remove_folder && context.remove_folder && !protected_folder) {
    const char *label =
        megatoy::platform::is_web() ? "Delete Folder..." : "Remove Folder";
    if (ImGui::MenuItem(label)) {
      context.pending_remove_folder = entry.full_path;
    }
    ImGui::Separator();
  }

  if (ImGui::MenuItem("Refresh repository")) {
    context.pending_menu_action = PendingMenuAction::Refresh;
  }

  ImGui::EndPopup();
}

void render_filter_bar(PatchSelectorContext &context) {
  auto &prefs = context.prefs;
  char search_buffer[128];
  std::strncpy(search_buffer, prefs.metadata_search_query.c_str(),
               sizeof(search_buffer));
  search_buffer[sizeof(search_buffer) - 1] = '\0';

  ImGui::SetNextItemWidth(ui::scale::px(130.0f));
  if (ImGui::InputTextWithHint("##SharedSearch",
                               ICON_FA_MAGNIFYING_GLASS " Search...",
                               search_buffer, sizeof(search_buffer))) {
    prefs.metadata_search_query = std::string(search_buffer);
  }
  prefs.patch_search_query = prefs.metadata_search_query;

  ImGui::SameLine();
  ImGui::SetNextItemWidth(ui::scale::px(60.0f));
  prefs.metadata_star_filter = std::clamp(prefs.metadata_star_filter, 0, 5);
  ImGui::SliderInt("##Stars", &prefs.metadata_star_filter, 0, 5,
                   prefs.metadata_star_filter == 0
                       ? "Stars"
                       : kStarLabels[prefs.metadata_star_filter].data(),
                   ImGuiSliderFlags_AlwaysClamp);

  ImGui::SameLine();
  const bool is_filtered =
      !(prefs.metadata_search_query.empty() && prefs.metadata_star_filter == 0);
  ImGui::BeginDisabled(!is_filtered);
  if (is_filtered) {
    if (ImGui::TextLink("Clear filters")) {
      prefs.metadata_search_query.clear();
      prefs.metadata_star_filter = 0;
      prefs.patch_search_query.clear();
    }
  } else {
    ImGui::Text("Clear filters");
  }
  ImGui::EndDisabled();
}

} // namespace ui::selector_detail
