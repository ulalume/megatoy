#include "patch_table_view.hpp"

#include "common.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include "patch_selector_shared.hpp"

#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ui::selector_detail {

namespace {

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

std::vector<const patches::PatchEntry *>
filtered_patches(PatchSelectorContext &context) {
  std::vector<const patches::PatchEntry *> all;
  collect_leaf_patches(context.repository.tree(), all);

  const std::string query_lower = to_lower(context.prefs.metadata_search_query);
  const int star_filter = context.prefs.metadata_star_filter;

  std::vector<const patches::PatchEntry *> filtered;
  filtered.reserve(all.size());
  for (const auto *entry : all) {
    if (entry_passes_star_filter(*entry, star_filter) &&
        entry_matches_query(*entry, query_lower)) {
      filtered.push_back(entry);
    }
  }
  return filtered;
}

void sort_patches(std::vector<const patches::PatchEntry *> &patches,
                  const PatchSelectorContext &context) {
  std::sort(
      patches.begin(), patches.end(),
      [&context](const patches::PatchEntry *a, const patches::PatchEntry *b) {
        int result = 0;
        switch (context.get_sort_column()) {
        case TableSortColumn::Name:
          result = a->name.compare(b->name);
          break;
        case TableSortColumn::Category: {
          const std::string cat_a = a->metadata ? a->metadata->category : "";
          const std::string cat_b = b->metadata ? b->metadata->category : "";
          result = cat_a.compare(cat_b);
          break;
        }
        case TableSortColumn::StarRating: {
          const int rating_a = a->metadata ? a->metadata->star_rating : 0;
          const int rating_b = b->metadata ? b->metadata->star_rating : 0;
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
}

void apply_table_sort_specs(PatchSelectorContext &context) {
  ImGuiTableSortSpecs *sort_specs = ImGui::TableGetSortSpecs();
  if (sort_specs == nullptr || !sort_specs->SpecsDirty) {
    return;
  }
  if (sort_specs->SpecsCount > 0) {
    // Column order in the table: Name, Stars, Category, Format, Path.
    static constexpr TableSortColumn kByIndex[] = {
        TableSortColumn::Name, TableSortColumn::StarRating,
        TableSortColumn::Category, TableSortColumn::Format,
        TableSortColumn::Path};
    const ImGuiTableColumnSortSpecs *spec = &sort_specs->Specs[0];
    if (spec->ColumnIndex < IM_ARRAYSIZE(kByIndex)) {
      context.set_sort_column(kByIndex[spec->ColumnIndex]);
    }
    context.set_sort_order(spec->SortDirection == ImGuiSortDirection_Ascending
                               ? SortOrder::Ascending
                               : SortOrder::Descending);
  }
  sort_specs->SpecsDirty = false;
}

/**
 * In-flight cell edits, keyed by relative path.
 *
 * ImGui reports a value every frame while a slider or text box is being
 * dragged or typed in; committing to the sidecar on each of those would
 * rewrite the file dozens of times per edit. The value is parked here until
 * the widget deactivates, then written once.
 */
struct PendingEdits {
  std::unordered_map<std::string, int> stars;
  std::unordered_map<std::string, std::string> categories;

  void drop_stale(const std::vector<const patches::PatchEntry *> &visible) {
    std::unordered_set<std::string> paths;
    paths.reserve(visible.size());
    for (const auto *entry : visible) {
      paths.insert(entry->relative_path);
    }
    std::erase_if(stars,
                  [&](const auto &kv) { return !paths.count(kv.first); });
    std::erase_if(categories,
                  [&](const auto &kv) { return !paths.count(kv.first); });
  }
};

struct PatchTableCache {
  const patches::PatchRepository *repository = nullptr;
  std::uint64_t repository_revision = 0;
  std::string search_query;
  int star_filter = 0;
  TableSortColumn sort_column = TableSortColumn::Name;
  SortOrder sort_order = SortOrder::Ascending;
  std::vector<const patches::PatchEntry *> rows;
  /// Whether the last get() rebuilt the row set, i.e. whether entries may have
  /// disappeared since the previous frame.
  bool rebuilt = false;

  const std::vector<const patches::PatchEntry *> &
  get(PatchSelectorContext &context) {
    const auto revision = context.repository.revision();
    const auto column = context.get_sort_column();
    const auto order = context.get_sort_order();
    rebuilt = false;
    if (repository != &context.repository || repository_revision != revision ||
        search_query != context.prefs.metadata_search_query ||
        star_filter != context.prefs.metadata_star_filter ||
        sort_column != column || sort_order != order) {
      repository = &context.repository;
      repository_revision = revision;
      search_query = context.prefs.metadata_search_query;
      star_filter = context.prefs.metadata_star_filter;
      sort_column = column;
      sort_order = order;
      rows = filtered_patches(context);
      sort_patches(rows, context);
      rebuilt = true;
    }
    return rows;
  }
};

patches::PatchMetadata prepare_metadata(PatchSelectorContext &context,
                                        const patches::PatchEntry &entry) {
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
}

/// Star slider cell. Returns true if the sidecar changed.
bool render_star_cell(PatchSelectorContext &context,
                      const patches::PatchEntry &entry, PendingEdits &edits) {
  int star_rating = entry.metadata ? entry.metadata->star_rating : 0;
  if (auto pending = edits.stars.find(entry.relative_path);
      pending != edits.stars.end()) {
    star_rating = pending->second;
  }

  ImGui::SetNextItemWidth(-1);
  const float available_width = ImGui::GetContentRegionAvail().x;
  if (ImGui::SliderInt("##star", &star_rating, 0, 5,
                       available_width > ui::scale::px(60.0f)
                           ? kStarLabels[star_rating].data()
                           : kStarLabelsMini[star_rating].data(),
                       ImGuiSliderFlags_AlwaysClamp)) {
    edits.stars[entry.relative_path] = star_rating;
  }

  bool changed = false;
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    if (!entry.metadata || entry.metadata->star_rating != star_rating) {
      auto metadata = prepare_metadata(context, entry);
      metadata.star_rating = star_rating;
      changed = context.repository.update_patch_metadata(entry.relative_path,
                                                         metadata);
    }
    edits.stars.erase(entry.relative_path);
  }
  return changed;
}

/// Category text cell. Returns true if the sidecar changed.
bool render_category_cell(PatchSelectorContext &context,
                          const patches::PatchEntry &entry,
                          PendingEdits &edits) {
  std::string category = entry.metadata ? entry.metadata->category : "";
  if (auto pending = edits.categories.find(entry.relative_path);
      pending != edits.categories.end()) {
    category = pending->second;
  }

  char buffer[64];
  std::strncpy(buffer, category.c_str(), sizeof(buffer));
  buffer[sizeof(buffer) - 1] = '\0';

  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputText("##category", buffer, sizeof(buffer))) {
    edits.categories[entry.relative_path] = std::string(buffer);
  }

  bool changed = false;
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    std::string new_category;
    if (auto pending = edits.categories.find(entry.relative_path);
        pending != edits.categories.end()) {
      new_category = pending->second;
    } else {
      new_category = std::string(buffer);
    }

    if (!entry.metadata || entry.metadata->category != new_category) {
      auto metadata = prepare_metadata(context, entry);
      metadata.category = std::move(new_category);
      changed = context.repository.update_patch_metadata(entry.relative_path,
                                                         metadata);
    }
    edits.categories.erase(entry.relative_path);
  }
  return changed;
}

} // namespace

void render_patch_table(PatchSelectorContext &context) {
  static PatchTableCache cache;
  static PendingEdits edits;
  const auto &patches = cache.get(context);
  if (cache.rebuilt) {
    edits.drop_stale(patches);
  }

  const std::string &current_selection_path =
      context.session.current_patch_selection_path();
  bool refresh_required = false;

  // The column widths below are proportional weights, which ImGui only
  // accepts under an explicit stretch sizing policy -- 1.92 turned that
  // former silent assumption into a user-error report.
  if (ImGui::BeginTable("PatchMetadataTable", 5,
                        ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable |
                            ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort, 0.3f);
    ImGui::TableSetupColumn("Stars", ImGuiTableColumnFlags_None, 0.1f);
    ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_None, 0.2f);
    ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_None, 0.15f);
    ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_None, 0.25f);
    ImGui::TableHeadersRow();

    apply_table_sort_specs(context);

    // Every row is one framed line high, so the clipper can size the list
    // from the first one it draws.
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(patches.size()));
    while (clipper.Step()) {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
        const auto *entry = patches[static_cast<size_t>(i)];
        ImGui::PushID(i);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        const bool is_current = !current_selection_path.empty() &&
                                current_selection_path == entry->relative_path;
        if (is_current) {
          ImGui::PushStyleColor(
              ImGuiCol_Text, styles::color(styles::MegatoyCol::TextHighlight));
        }
        const bool name_selected =
            ImGui::Selectable(entry->name.c_str(), false);
        if (is_current) {
          ImGui::PopStyleColor();
        }
        if (name_selected && context.safe_load_patch) {
          context.safe_load_patch(*entry);
        }
        entry_context_menu(context, *entry);

        const bool can_edit_metadata =
            context.repository.can_edit_metadata(*entry);
        ImGui::BeginDisabled(!can_edit_metadata);
        ImGui::TableSetColumnIndex(1);
        refresh_required |= render_star_cell(context, *entry, edits);

        ImGui::TableSetColumnIndex(2);
        refresh_required |= render_category_cell(context, *entry, edits);
        ImGui::EndDisabled();

        ImGui::TableSetColumnIndex(3);
        if (is_current) {
          ImGui::TextColored(styles::color(styles::MegatoyCol::TextHighlight),
                             "%s", entry->format.c_str());
        } else {
          ImGui::Text("%s", entry->format.c_str());
        }

        ImGui::TableSetColumnIndex(4);
        const std::string display_path =
            display_preset_path(entry->relative_path);
        if (is_current) {
          ImGui::TextColored(styles::color(styles::MegatoyCol::TextHighlight),
                             "%s", display_path.c_str());
        } else {
          ImGui::TextDisabled("%s", display_path.c_str());
        }

        ImGui::PopID();
      }
    }

    ImGui::EndTable();
  }

  if (refresh_required) {
    context.repository.refresh();
  }
}

} // namespace ui::selector_detail
