#include "patch_selector_shared.hpp"

#include "common.hpp"
#include "file_manager.hpp"
#include "gui/save_export_actions.hpp"
#include "gui/ui_scale.hpp"
#include "patch_tree_view.hpp"
#include "patches/patch_entry_actions.hpp"
#include "platform/platform_config.hpp"
#include "star_rating.hpp"

#include <IconsFontAwesome7.h>
#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <imgui_internal.h>
#include <optional>

namespace ui::selector_detail {

void show_patch_tooltip(const patches::PatchRepository &repository,
                        const patches::PatchEntry &entry) {
  if (!ImGui::IsItemHovered()) {
    return;
  }

  std::string tooltip =
      "Format: " + entry.format + "\nPath: " +
      display_preset_path(repository.display_path(entry.relative_path));

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

bool entry_can_rename(const PatchSelectorContext &context,
                      const patches::PatchEntry &entry) {
  const bool protected_folder = context.folder_is_protected &&
                                context.folder_is_protected(entry.full_path);
  return patches::entry_can_rename(context.repository, entry,
                                   static_cast<bool>(context.rename_patch),
                                   protected_folder);
}

bool entry_can_delete(const PatchSelectorContext &context,
                      const patches::PatchEntry &entry) {
  return patches::entry_can_delete(context.repository, entry,
                                   static_cast<bool>(context.delete_patch));
}

void entry_context_menu(PatchSelectorContext &context,
                        const patches::PatchEntry &entry,
                        bool allow_remove_folder) {
  if (!ImGui::BeginPopupContextItem(nullptr)) {
    return;
  }

  const patches::EntryMenuCapabilities available{
      .create_patch = static_cast<bool>(context.create_patch_in),
      .create_folder = static_cast<bool>(context.create_folder_in),
      .save_current = static_cast<bool>(context.save_current_patch),
      .download_folder = static_cast<bool>(context.download_entry),
      .download_current = static_cast<bool>(context.download_current_patch),
      .download_patch = static_cast<bool>(context.download_patch_entry),
      .save_to_storage =
          static_cast<bool>(context.save_current_patch_to_storage),
      .save_as = static_cast<bool>(context.save_current_patch_as),
      .duplicate = static_cast<bool>(context.duplicate_patch),
      .rename = static_cast<bool>(context.rename_patch),
      .delete_patch = static_cast<bool>(context.delete_patch),
      .remove_folder = static_cast<bool>(context.remove_folder),
      .reveal = static_cast<bool>(context.reveal_in_file_manager)};
  const bool protected_folder = context.folder_is_protected &&
                                context.folder_is_protected(entry.full_path);
  const auto actions = patches::entry_menu_actions(
      context.session, entry, available, protected_folder, allow_remove_folder);

  // Separators sit between groups, so one is drawn only once the group above
  // it has something in it.
  bool anything_drawn = false;
  auto begin_group = [&anything_drawn](bool has_items) {
    if (!has_items) {
      return false;
    }
    if (anything_drawn) {
      ImGui::Separator();
    }
    anything_drawn = true;
    return true;
  };

  if (begin_group(actions.create_patch || actions.create_folder ||
                  actions.save_current || actions.download_folder ||
                  actions.download_patch ||
                  (actions.save_to_storage || actions.save_as) ||
                  actions.duplicate)) {
    if (actions.create_patch && ImGui::MenuItem("New patch...")) {
      context.create_patch_in(entry.full_path);
    }
    if (actions.create_folder && ImGui::MenuItem("New folder...")) {
      // Open now, so the new folder is in view once it exists.
      open_tree_directory(entry.relative_path);
      context.create_folder_in(entry.full_path);
    }
    if (actions.save_current) {
      ImGui::BeginDisabled(!actions.save_enabled);
      if (ImGui::MenuItem(context.session.save_label_for(true))) {
        context.pending_menu_action = PendingMenuAction::SaveCurrent;
      }
      ImGui::EndDisabled();
    }
    if (actions.download_folder && ImGui::MenuItem("Download")) {
      context.download_entry(entry);
    }
    std::optional<std::string> download_extension;
    if (actions.download_patch) {
      download_extension = ui::download_format_menu(context.session);
    }
    if (actions.save_to_storage || actions.save_as) {
      if (actions.save_to_storage) {
        if (ImGui::MenuItem("Save to browser storage...")) {
          context.save_current_patch_to_storage();
        }
      } else if (ImGui::MenuItem("Save As...")) {
        context.save_current_patch_as();
      }
    }
    if (actions.duplicate && ImGui::MenuItem("Duplicate...")) {
      context.duplicate_patch(entry);
    }
    if (download_extension) {
      if (actions.is_current) {
        context.download_current_patch(*download_extension);
      } else {
        context.download_patch_entry(entry, *download_extension);
      }
    }
  }

  // Written once the tree is drawn: the write rebuilds it, and `entry`
  // with it.
  if (begin_group(actions.rate) && ImGui::BeginMenu("Rating")) {
    const int rating = entry.metadata ? entry.metadata->star_rating : 0;
    for (int stars = 0; stars <= 5; ++stars) {
      const char *label = stars == 0 ? "None" : kStarLabels[stars].data();
      if (ImGui::MenuItem(label, nullptr, stars == rating)) {
        context.pending_rating = PendingRating{entry.relative_path, stars};
      }
    }
    ImGui::EndMenu();
  }

  // Whether a file manager exists is the composition root's call -- the
  // callback is simply absent on platforms without one.
  if (begin_group(actions.reveal)) {
    if (ImGui::MenuItem(ui::reveal_in_file_manager_label())) {
      context.reveal_in_file_manager(
          context.repository.to_absolute_path(entry.relative_path));
    }
  }

  if (begin_group(actions.rename || actions.delete_patch)) {
    if (actions.rename && ImGui::MenuItem("Rename...")) {
      context.rename_patch(entry);
    }
    if (actions.delete_patch && ImGui::MenuItem("Delete...")) {
      context.delete_patch(entry);
    }
  }

  if (begin_group(actions.remove_folder)) {
    const char *label =
        megatoy::platform::is_web() ? "Delete Folder..." : "Remove Folder";
    if (ImGui::MenuItem(label)) {
      context.pending_remove_folder = entry.full_path;
    }
  }

  ImGui::EndPopup();
}

void render_filter_bar(PatchSelectorContext &context) {
  auto &prefs = context.prefs;
  char search_buffer[128];
  std::strncpy(search_buffer, prefs.metadata_search_query.c_str(),
               sizeof(search_buffer));
  search_buffer[sizeof(search_buffer) - 1] = '\0';

  // The search box takes what the star filter leaves of the row, up to where
  // the caller has narrowed it to.
  const ImGuiStyle &style = ImGui::GetStyle();
  const float row_end = ImGui::GetCurrentWindow()->WorkRect.Max.x;
  const float search_width = std::max(
      row_end - ImGui::GetCursorScreenPos().x - star_rating_width() -
          style.ItemSpacing.x,
      ui::scale::px(60.0f));
  ImGui::SetNextItemWidth(search_width);
  // The clear button sits over the box's right end and has to win the hover.
  ImGui::SetNextItemAllowOverlap();
  if (ImGui::InputTextWithHint("##SharedSearch",
                               ICON_FA_MAGNIFYING_GLASS " Search...",
                               search_buffer, sizeof(search_buffer))) {
    prefs.metadata_search_query = std::string(search_buffer);
  }
  if (!prefs.metadata_search_query.empty()) {
    const ImVec2 box_min = ImGui::GetItemRectMin();
    const ImVec2 box_max = ImGui::GetItemRectMax();
    const float size = box_max.y - box_min.y;
    ImGui::SetCursorScreenPos(ImVec2(box_max.x - size, box_min.y));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (ImGui::Button(ICON_FA_XMARK "##clear_search", ImVec2(size, size))) {
      prefs.metadata_search_query.clear();
    }
    ImGui::PopStyleColor(2);
  }
  prefs.patch_search_query = prefs.metadata_search_query;

  ImGui::SameLine();
  star_rating_slider("##Stars", prefs.metadata_star_filter, "Stars");
}

} // namespace ui::selector_detail
