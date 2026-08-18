#include "patch_selector_shared.hpp"

#include "common.hpp"
#include "file_manager.hpp"
#include "platform/platform_config.hpp"

#include <IconsFontAwesome7.h>
#include <cctype>
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

std::string to_lower(const std::string &value) {
  std::string lowered;
  lowered.reserve(value.size());
  for (unsigned char ch : value) {
    lowered.push_back(static_cast<char>(std::tolower(ch)));
  }
  return lowered;
}

bool contains_case_insensitive(const std::string &haystack,
                               const std::string &needle_lower) {
  if (needle_lower.empty()) {
    return true;
  }
  return to_lower(haystack).find(needle_lower) != std::string::npos;
}

bool entry_passes_star_filter(const patches::PatchEntry &entry,
                              int min_star_rating) {
  if (min_star_rating <= 0) {
    return true;
  }
  const int rating = entry.metadata ? entry.metadata->star_rating : 0;
  return rating >= min_star_rating;
}

bool entry_matches_query(const patches::PatchEntry &entry,
                         const std::string &query_lower) {
  if (query_lower.empty()) {
    return true;
  }
  if (contains_case_insensitive(entry.name, query_lower)) {
    return true;
  }
  return entry.metadata &&
         contains_case_insensitive(entry.metadata->category, query_lower);
}

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
        context.save_current_patch();
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

  if ((!entry.is_directory && !is_current) || is_current ||
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

  if (context.delete_patch && context.repository.can_delete_patch(entry)) {
    if (ImGui::MenuItem("Delete...")) {
      context.delete_patch(entry);
    }
    ImGui::Separator();
  }

  const bool protected_folder = context.folder_is_protected &&
                                context.folder_is_protected(entry.full_path);
  if (allow_remove_folder && context.remove_folder && !protected_folder) {
    const char *label =
        megatoy::platform::is_web() ? "Delete Folder..." : "Remove Folder";
    if (ImGui::MenuItem(label)) {
      context.pending_remove_folder = entry.full_path;
    }
    ImGui::Separator();
  }

  if (ImGui::MenuItem("Refresh repository")) {
    context.repository.refresh();
  }

  ImGui::EndPopup();
}

void render_filter_bar(PatchSelectorContext &context) {
  auto &prefs = context.prefs;
  char search_buffer[128];
  std::strncpy(search_buffer, prefs.metadata_search_query.c_str(),
               sizeof(search_buffer));
  search_buffer[sizeof(search_buffer) - 1] = '\0';

  ImGui::SetNextItemWidth(130);
  if (ImGui::InputTextWithHint("##SharedSearch",
                               ICON_FA_MAGNIFYING_GLASS " Search...",
                               search_buffer, sizeof(search_buffer))) {
    prefs.metadata_search_query = std::string(search_buffer);
  }
  prefs.patch_search_query = prefs.metadata_search_query;

  ImGui::SameLine();
  ImGui::SetNextItemWidth(60);
  ImGui::SliderInt("##Stars", &prefs.metadata_star_filter, 0, 5,
                   prefs.metadata_star_filter == 0
                       ? "Stars"
                       : kStarLabels[prefs.metadata_star_filter].data());

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
