#include "patch_selector.hpp"
#include "common.hpp"
#include "file_manager.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <imgui.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "gui/styles/megatoy_style.hpp"

namespace ui {
namespace {
#define INDENT (4.0f)

constexpr std::string_view kBuiltinRootName{"presets"};
constexpr std::string_view kBuiltinDisplayName{"Default Presets"};

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

  std::string haystack_lower = to_lower(haystack);
  return haystack_lower.find(needle_lower) != std::string::npos;
}

bool has_search_text(const std::string &value) {
  return std::any_of(value.begin(), value.end(),
                     [](unsigned char ch) { return !std::isspace(ch); });
}

std::string format_display_path(const std::string &relative_path) {
  if (relative_path.rfind(kBuiltinRootName, 0) != 0) {
    return relative_path;
  }

  if (relative_path.size() == kBuiltinRootName.size()) {
    return std::string(kBuiltinDisplayName);
  }

  if (relative_path.size() > kBuiltinRootName.size() &&
      relative_path[kBuiltinRootName.size()] == '/') {
    return std::string(kBuiltinDisplayName) +
           relative_path.substr(kBuiltinRootName.size());
  }

  return relative_path;
}

void collect_leaf_patches(const std::vector<patches::PatchEntry> &tree,
                          std::vector<const patches::PatchEntry *> &out) {
  for (const auto &item : tree) {
    if (item.is_directory) {
      collect_leaf_patches(item.children, out);
    } else {
      out.push_back(&item);
    }
  }
}

void begin_popup_context(PatchSelectorContext &context,
                         const std::string &relative_path) {
  if (ImGui::BeginPopupContextItem(nullptr)) {
    if (ImGui::MenuItem(ui::reveal_in_file_manager_label())) {
      if (context.reveal_in_file_manager) {
        context.reveal_in_file_manager(
            context.repository.to_absolute_path(relative_path));
      }
    }
    ImGui::EndPopup();
  }
}
void is_item_hovered(const std::string &format,
                     const std::string &relative_path) {
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Format: %s\nPath: %s", format.c_str(),
                      relative_path.c_str());
  }
}

void render_patch_tree(const std::vector<patches::PatchEntry> &tree,
                       PatchSelectorContext &context, int depth = 0) {
  for (const auto &item : tree) {
    ImGui::PushID(item.relative_path.c_str());

    if (item.is_directory) {
      if (ImGui::TreeNode(item.name.c_str())) {
        begin_popup_context(context, item.relative_path);
        render_patch_tree(item.children, context, depth + 1);
        ImGui::TreePop();
      }
    } else {
      ImGui::Indent(INDENT * depth);

      bool is_current =
          (item.relative_path == context.session.current_patch_path());
      if (is_current) {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              styles::color(styles::MegatoyCol::TextHighlight));
      }
      std::string name_string = item.name;
      auto name_string_selectable =
          ImGui::Selectable(name_string.c_str(), false);
      begin_popup_context(context, item.relative_path);
      is_item_hovered(item.format, item.relative_path);
      ImGui::SameLine();
      ImGui::PushStyleColor(
          ImGuiCol_Text,
          color_with_alpha_vec4(ImGui::GetStyleColorVec4(ImGuiCol_Text), 0.5f));
      auto format_string_selectable =
          ImGui::Selectable(item.format.c_str(), false);
      if (name_string_selectable || format_string_selectable) {
        if (context.safe_load_patch) {
          context.safe_load_patch(item);
        }
      }
      ImGui::PopStyleColor();
      begin_popup_context(context, item.relative_path);
      is_item_hovered(item.format, item.relative_path);
      if (is_current) {
        ImGui::PopStyleColor();
      }
      ImGui::Unindent(INDENT * depth);
    }
    ImGui::PopID();
  }
}

void render_metadata_table(PatchSelectorContext &context) {
  // Get all patches for table view
  std::vector<const patches::PatchEntry *> all_patches;
  collect_leaf_patches(context.repository.tree(), all_patches);

  // Filter patches based on metadata criteria
  std::vector<const patches::PatchEntry *> filtered_patches;
  filtered_patches.reserve(all_patches.size());

  const bool has_metadata_search =
      has_search_text(context.prefs.metadata_search_query);
  std::string metadata_query_lower;
  if (has_metadata_search) {
    metadata_query_lower = to_lower(context.prefs.metadata_search_query);
  }
  const int star_filter = context.prefs.metadata_star_filter;

  for (const auto *entry : all_patches) {
    bool matches = true;

    // Search query filter
    if (has_metadata_search) {
      if (!contains_case_insensitive(entry->name, metadata_query_lower) &&
          (!entry->metadata ||
           !contains_case_insensitive(entry->metadata->category,
                                      metadata_query_lower))) {
        matches = false;
      }
    }

    // Star rating filter
    if (matches && star_filter > 0) {
      const int rating = entry->metadata ? entry->metadata->star_rating : 0;
      if (rating < star_filter) {
        matches = false;
      }
    }

    if (matches) {
      filtered_patches.push_back(entry);
    }
  }

  static std::unordered_map<std::string, int> pending_star_edits;
  static std::unordered_map<std::string, std::string> pending_category_edits;

  // Remove stale editing states for entries no longer visible
  {
    std::unordered_set<std::string> visible_paths;
    visible_paths.reserve(filtered_patches.size());
    for (const auto *entry : filtered_patches) {
      visible_paths.insert(entry->relative_path);
    }

    for (auto it = pending_star_edits.begin();
         it != pending_star_edits.end();) {
      if (visible_paths.find(it->first) == visible_paths.end()) {
        it = pending_star_edits.erase(it);
      } else {
        ++it;
      }
    }
    for (auto it = pending_category_edits.begin();
         it != pending_category_edits.end();) {
      if (visible_paths.find(it->first) == visible_paths.end()) {
        it = pending_category_edits.erase(it);
      } else {
        ++it;
      }
    }
  }

  // Sort patches based on current sort criteria
  std::sort(
      filtered_patches.begin(), filtered_patches.end(),
      [&context](const patches::PatchEntry *a, const patches::PatchEntry *b) {
        int result = 0;

        switch (context.get_sort_column()) {
        case TableSortColumn::Name:
          result = a->name.compare(b->name);
          break;
        case TableSortColumn::Category: {
          std::string cat_a = a->metadata ? a->metadata->category : "";
          std::string cat_b = b->metadata ? b->metadata->category : "";
          result = cat_a.compare(cat_b);
          break;
        }
        case TableSortColumn::StarRating: {
          int rating_a = a->metadata ? a->metadata->star_rating : 0;
          int rating_b = b->metadata ? b->metadata->star_rating : 0;
          result = rating_a - rating_b;
          break;
        }
        case TableSortColumn::Format:
          result = a->format.compare(b->format);
          break;
        case TableSortColumn::Path:
          result = a->relative_path.compare(b->relative_path);
          break;
        }

        return context.get_sort_order() == SortOrder::Ascending ? result < 0
                                                                : result > 0;
      });

  const std::string &current_relative_path =
      context.session.current_patch_path();

  auto prepare_metadata = [&](const patches::PatchEntry &entry) {
    patches::PatchMetadata metadata =
        entry.metadata.value_or(patches::PatchMetadata{});
    metadata.path = entry.relative_path;
    if (metadata.hash.empty()) {
      ym2612::Patch patch;
      if (context.repository.load_patch(entry, patch)) {
        metadata.hash = patch.hash();
      }
    }
    return metadata;
  };

  bool refresh_required = false;

  // Render table
  if (ImGui::BeginTable("PatchMetadataTable", 5,
                        ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable |
                            ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg)) {

    // Setup columns
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort, 0.3f);
    ImGui::TableSetupColumn("★", ImGuiTableColumnFlags_None, 0.1f);
    ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_None, 0.2f);
    ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_None, 0.15f);
    ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_None, 0.25f);
    ImGui::TableHeadersRow();

    // Handle sorting
    ImGuiTableSortSpecs *sort_specs = ImGui::TableGetSortSpecs();
    if (sort_specs && sort_specs->SpecsDirty) {
      if (sort_specs->SpecsCount > 0) {
        const ImGuiTableColumnSortSpecs *spec = &sort_specs->Specs[0];

        switch (spec->ColumnIndex) {
        case 0:
          context.set_sort_column(TableSortColumn::Name);
          break;
        case 1:
          context.set_sort_column(TableSortColumn::StarRating);
          break;
        case 2:
          context.set_sort_column(TableSortColumn::Category);
          break;
        case 3:
          context.set_sort_column(TableSortColumn::Format);
          break;
        case 4:
          context.set_sort_column(TableSortColumn::Path);
          break;
        }

        context.set_sort_order(
            (spec->SortDirection == ImGuiSortDirection_Ascending)
                ? SortOrder::Ascending
                : SortOrder::Descending);
      }
      sort_specs->SpecsDirty = false;
    }

    // Render rows
    for (size_t i = 0; i < filtered_patches.size(); ++i) {
      const auto *entry = filtered_patches[i];
      ImGui::PushID(static_cast<int>(i));

      ImGui::TableNextRow();

      // Name column
      ImGui::TableSetColumnIndex(0);
      bool is_current = !current_relative_path.empty() &&
                        current_relative_path == entry->relative_path;

      if (is_current) {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              styles::color(styles::MegatoyCol::TextHighlight));
      }

      bool name_selected = ImGui::Selectable(entry->name.c_str(), is_current);
      if (is_current) {
        ImGui::PopStyleColor();
      }

      if (name_selected && context.safe_load_patch) {
        if (context.safe_load_patch) {
          context.safe_load_patch(*entry);
        }
      }

      begin_popup_context(context, entry->relative_path);

      // Star rating column
      ImGui::TableSetColumnIndex(1);
      int star_rating = entry->metadata ? entry->metadata->star_rating : 0;
      if (auto pending = pending_star_edits.find(entry->relative_path);
          pending != pending_star_edits.end()) {
        star_rating = pending->second;
      }
      ImGui::SetNextItemWidth(-1);
      bool star_changed = ImGui::SliderInt("##star", &star_rating, 0, 4, "%d★");
      if (star_changed) {
        pending_star_edits[entry->relative_path] = star_rating;
      }
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        bool needs_update =
            !entry->metadata || entry->metadata->star_rating != star_rating;
        if (needs_update) {
          auto metadata = prepare_metadata(*entry);
          metadata.star_rating = star_rating;
          if (context.repository.update_patch_metadata(entry->relative_path,
                                                       metadata)) {
            refresh_required = true;
          }
        }
        pending_star_edits.erase(entry->relative_path);
      }

      // Category column
      ImGui::TableSetColumnIndex(2);
      std::string category = entry->metadata ? entry->metadata->category : "";
      if (auto pending = pending_category_edits.find(entry->relative_path);
          pending != pending_category_edits.end()) {
        category = pending->second;
      }
      char category_buffer[64];
      std::strncpy(category_buffer, category.c_str(), sizeof(category_buffer));
      category_buffer[sizeof(category_buffer) - 1] = '\0';

      ImGui::SetNextItemWidth(-1);
      bool category_changed = ImGui::InputText("##category", category_buffer,
                                               sizeof(category_buffer));
      if (category_changed) {
        pending_category_edits[entry->relative_path] =
            std::string(category_buffer);
      }
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        std::string new_category;
        if (auto pending = pending_category_edits.find(entry->relative_path);
            pending != pending_category_edits.end()) {
          new_category = pending->second;
        } else {
          new_category = std::string(category_buffer);
        }

        if (!entry->metadata || entry->metadata->category != new_category) {
          auto metadata = prepare_metadata(*entry);
          metadata.category = std::move(new_category);
          if (context.repository.update_patch_metadata(entry->relative_path,
                                                       metadata)) {
            refresh_required = true;
          }
        }
        pending_category_edits.erase(entry->relative_path);
      }

      // Format column
      ImGui::TableSetColumnIndex(3);
      if (is_current) {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              styles::color(styles::MegatoyCol::TextHighlight));
      }
      ImGui::Text("%s", entry->format.c_str());
      if (is_current) {
        ImGui::PopStyleColor();
      }

      // Path column
      ImGui::TableSetColumnIndex(4);
      const std::string display_path =
          format_display_path(entry->relative_path);
      if (is_current) {
        ImGui::TextColored(styles::color(styles::MegatoyCol::TextHighlight),
                           "%s", display_path.c_str());
      } else {
        ImGui::TextDisabled("%s", display_path.c_str());
      }

      ImGui::PopID();
    }

    ImGui::EndTable();
  }

  if (refresh_required) {
    context.repository.refresh();
  }
}

} // namespace

void render_patch_selector(const char *title, PatchSelectorContext &context) {
  auto &prefs = context.prefs;
  if (!prefs.show_patch_selector) {
    return;
  }

  ImGui::SetNextWindowPos(ImVec2(50, 400), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(350, 500), ImGuiCond_FirstUseEver);

  if (ImGui::Begin(title, &prefs.show_patch_selector)) {
    auto &preset_repository = context.repository;
    if (preset_repository.has_directory_changed()) {
      preset_repository.refresh();
    }

    ImGui::SameLine();

    // View mode buttons with active state indication
    PatchViewMode current_mode = context.get_view_mode();
    bool tree_query_active = has_search_text(prefs.patch_search_query);
    if (current_mode == PatchViewMode::Search && !tree_query_active) {
      context.set_view_mode(PatchViewMode::Tree);
      current_mode = PatchViewMode::Tree;
    }

    const bool tree_button_active = current_mode == PatchViewMode::Tree ||
                                    current_mode == PatchViewMode::Search;

    if (tree_button_active) {
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    if (ImGui::Button("Tree")) {
      context.set_view_mode(PatchViewMode::Tree);
    }
    if (tree_button_active) {
      ImGui::PopStyleColor();
    }

    current_mode = context.get_view_mode();

    ImGui::SameLine();
    if (current_mode == PatchViewMode::Table) {
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    if (ImGui::Button("Table")) {
      context.set_view_mode(PatchViewMode::Table);
    }
    if (current_mode == PatchViewMode::Table) {
      ImGui::PopStyleColor();
    }

    current_mode = context.get_view_mode();

    ImGui::SameLine();

    if (ImGui::TextLink("Refresh directory")) {
      context.repository.refresh();
    }

    ImGui::Spacing();

    // Search input (used for all modes)
    char search_buffer[128];
    const bool table_mode = (current_mode == PatchViewMode::Table);
    const char *search_hint =
        table_mode ? "Search patches and metadata" : "Type to filter patches";

    if (table_mode) {
      std::strncpy(search_buffer, context.prefs.metadata_search_query.c_str(),
                   sizeof(search_buffer));
    } else {
      std::strncpy(search_buffer, prefs.patch_search_query.c_str(),
                   sizeof(search_buffer));
    }
    search_buffer[sizeof(search_buffer) - 1] = '\0';

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputTextWithHint("##Search", search_hint, search_buffer,
                                 sizeof(search_buffer))) {
      if (table_mode) {
        context.prefs.metadata_search_query = std::string(search_buffer);
      } else {
        prefs.patch_search_query = std::string(search_buffer);
        bool has_query_now = has_search_text(prefs.patch_search_query);
        context.set_view_mode(has_query_now ? PatchViewMode::Search
                                            : PatchViewMode::Tree);
        current_mode = context.get_view_mode();
        tree_query_active = has_query_now;
      }
    }
    tree_query_active = has_search_text(prefs.patch_search_query);

    // Additional filters for table view
    if (context.get_view_mode() == PatchViewMode::Table) {
      ImGui::Spacing();

      // Star rating filter
      ImGui::SetNextItemWidth(100);
      ImGui::SliderInt("Min Stars", &context.prefs.metadata_star_filter, 0, 4,
                       "%d★");

      ImGui::SameLine();
      if (ImGui::Button("Clear Filters")) {
        context.prefs.metadata_search_query.clear();
        context.prefs.metadata_star_filter = 0;
      }
    }

    ImGui::Spacing();

    auto preset_tree = preset_repository.tree();

    // Determine which view to render
    if (context.get_view_mode() == PatchViewMode::Table) {
      if (ImGui::BeginChild("MetadataTable", ImGui::GetContentRegionAvail(),
                            true)) {
        render_metadata_table(context);
      }
      ImGui::EndChild();
    } else {
      if (tree_query_active ||
          context.get_view_mode() == PatchViewMode::Search) {
        if (ImGui::BeginChild("PresetSearch", ImGui::GetContentRegionAvail(),
                              true)) {
          std::vector<const patches::PatchEntry *> all_patches;
          collect_leaf_patches(preset_tree, all_patches);

          std::string query_lower = to_lower(prefs.patch_search_query);

          size_t match_count = 0;
          for (const auto *entry : all_patches) {
            if (!contains_case_insensitive(entry->name, query_lower)) {
              continue;
            }

            match_count++;

            ImGui::PushID(entry->relative_path.c_str());

            bool is_current =
                context.session.current_patch_path() == entry->relative_path;

            if (is_current) {
              ImGui::PushStyleColor(
                  ImGuiCol_Text,
                  styles::color(styles::MegatoyCol::TextHighlight));
            }

            std::string label = "[" + entry->format + "] " + entry->name +
                                "##" + entry->relative_path;
            if (ImGui::Selectable(label.c_str(), is_current)) {
              if (context.safe_load_patch) {
                context.safe_load_patch(*entry);
              }
            }

            if (is_current) {
              ImGui::PopStyleColor();
            }

            begin_popup_context(context, entry->relative_path);
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("Format: %s\nPath: %s", entry->format.c_str(),
                                entry->relative_path.c_str());
            }

            ImGui::PopID();
          }

          if (match_count == 0) {
            ImGui::TextColored(styles::color(styles::MegatoyCol::TextMuted),
                               "No results for '%s'",
                               prefs.patch_search_query.c_str());
          }
        }
        ImGui::EndChild();
      } else {
        if (ImGui::BeginChild("PresetTree", ImGui::GetContentRegionAvail(),
                              true)) {
          render_patch_tree(preset_tree, context);
        }
        ImGui::EndChild();
      }
    }
  }

  ImGui::End();
}

} // namespace ui
