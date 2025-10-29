#pragma once

#include "patches/patch_repository.hpp"
#include "patches/patch_session.hpp"
#include "preferences/preference_manager.hpp"
#include <imgui.h>

#include <filesystem>
#include <functional>

namespace ui {

enum class PatchViewMode {
  Tree,   // Hierarchical folder view
  Search, // Flat search results
  Table   // Metadata table view
};

enum class TableSortColumn { Name, Category, StarRating, Format, Path };

enum class SortOrder { Ascending, Descending };

struct PatchSelectorContext {
  patches::PatchRepository &repository;
  patches::PatchSession &session;
  PreferenceManager::UIPreferences &prefs;
  std::function<void(const patches::PatchEntry &)> safe_load_patch;
  std::function<void(const std::filesystem::path &)> reveal_in_file_manager;

  // Helper methods to convert between enum and int values
  PatchViewMode get_view_mode() const {
    return static_cast<PatchViewMode>(prefs.patch_view_mode);
  }

  void set_view_mode(PatchViewMode mode) {
    prefs.patch_view_mode = static_cast<int>(mode);
  }

  TableSortColumn get_sort_column() const {
    return static_cast<TableSortColumn>(prefs.patch_sort_column);
  }

  void set_sort_column(TableSortColumn column) {
    prefs.patch_sort_column = static_cast<int>(column);
  }

  SortOrder get_sort_order() const {
    return static_cast<SortOrder>(prefs.patch_sort_order);
  }

  void set_sort_order(SortOrder order) {
    prefs.patch_sort_order = static_cast<int>(order);
  }
};

// Function to render patch browser panel
void render_patch_selector(const char *title, PatchSelectorContext &context);

} // namespace ui
