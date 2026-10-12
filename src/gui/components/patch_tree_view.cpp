#include "patch_tree_view.hpp"

#include "common.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include "patch_selector_shared.hpp"
#include "patch_tree_flatten.hpp"

#include <cstddef>
#include <cstdint>
#include <imgui.h>
#include <string>
#include <unordered_set>
#include <vector>

namespace ui::selector_detail {

namespace {

constexpr float kDepthIndent = 4.0f;

/**
 * The visible rows, rebuilt only when the tree, the filters or the expansion
 * state change. Expansion lives here too, because ImGui's own tree state is
 * unreachable for rows the clipper never draws.
 */
struct PatchTreeCache {
  const std::vector<patches::PatchEntry> *tree = nullptr;
  std::uint64_t repository_revision = 0;
  std::string search_query;
  int star_filter = 0;
  std::uint64_t built_open_revision = 0;

  /// Bumped on every expand/collapse, so the rows rebuild on the next frame.
  std::uint64_t open_revision = 1;
  std::unordered_set<std::string> open_directories;
  std::vector<TreeRow> rows;

  const std::vector<TreeRow> &
  get(const std::vector<patches::PatchEntry> &source, std::uint64_t revision,
      const std::string &query_lower, int min_star_rating) {
    if (tree != &source || repository_revision != revision ||
        search_query != query_lower || star_filter != min_star_rating ||
        built_open_revision != open_revision) {
      tree = &source;
      repository_revision = revision;
      search_query = query_lower;
      star_filter = min_star_rating;
      built_open_revision = open_revision;
      rows = flatten_visible_rows(source, query_lower, min_star_rating,
                                  open_directories);
    }
    return rows;
  }

  void set_open(const std::string &relative_path, bool open) {
    if (open) {
      open_directories.insert(relative_path);
    } else {
      open_directories.erase(relative_path);
    }
    ++open_revision;
  }

  void follow(const std::string &from, const std::string &to) {
    const std::string prefix = from + "/";
    std::vector<std::string> followed;
    for (const auto &path : open_directories) {
      if (path == from || path.rfind(prefix, 0) == 0) {
        followed.push_back(to + path.substr(from.size()));
      }
    }
    open_directories.insert(followed.begin(), followed.end());
    ++open_revision;
  }
};

constexpr const char *kEntryPayload = "MEGATOY_PATCH_ENTRY";

const patches::PatchEntry *
find_by_relative_path(const std::vector<patches::PatchEntry> &entries,
                      const std::string &relative_path) {
  for (const auto &entry : entries) {
    if (entry.relative_path == relative_path) {
      return &entry;
    }
    if (relative_path.rfind(entry.relative_path + "/", 0) == 0) {
      return find_by_relative_path(entry.children, relative_path);
    }
  }
  return nullptr;
}

/// Lets the last drawn item be dragged onto a folder. Workspace folders and
/// anything that cannot be renamed stay put.
void entry_drag_source(const PatchSelectorContext &context,
                       const patches::PatchEntry &entry, int depth) {
  if (!context.move_patch || depth == 0 || !ImGui::IsItemActive() ||
      !entry_can_rename(context, entry)) {
    return;
  }
  if (ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload(kEntryPayload, entry.relative_path.data(),
                              entry.relative_path.size());
    ImGui::TextUnformatted(entry.name.c_str());
    ImGui::EndDragDropSource();
  }
}

/// Makes the last drawn folder row accept the entries it can take.
void folder_drop_target(PatchSelectorContext &context,
                        const patches::PatchEntry &folder) {
  if (!context.move_patch || !ImGui::BeginDragDropTarget()) {
    return;
  }
  const ImGuiPayload *payload = ImGui::GetDragDropPayload();
  if (payload != nullptr && payload->IsDataType(kEntryPayload)) {
    const std::string relative_path(
        static_cast<const char *>(payload->Data),
        static_cast<std::size_t>(payload->DataSize));
    const auto *source =
        find_by_relative_path(context.repository.tree(), relative_path);
    if (source != nullptr && context.session.can_move_patch(*source, folder) &&
        ImGui::AcceptDragDropPayload(kEntryPayload) != nullptr) {
      context.pending_move = PendingMove{*source, folder};
    }
  }
  ImGui::EndDragDropTarget();
}

/// F2 renames and Delete or Backspace deletes the focused row, wherever its
/// context menu would offer the same.
void handle_row_keys(PatchSelectorContext &context,
                     const patches::PatchEntry &entry) {
  if (!ImGui::IsItemFocused() || ImGui::GetIO().WantTextInput ||
      !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
      ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                      ImGuiPopupFlags_AnyPopupLevel)) {
    return;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
    if (entry_can_rename(context, entry)) {
      context.rename_patch(entry);
    }
  } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
             ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
    if (entry_can_delete(context, entry)) {
      context.delete_patch(entry);
    }
  }
}

/**
 * Reproduces the layout of the recursion this list replaced: each level used
 * to sit inside one more TreePush (one IndentSpacing each) and file rows added
 * kDepthIndent per level on top. ImGui::Indent(0) means "one IndentSpacing",
 * so the old depth-0 file rows were indented as well.
 */
float row_indent(const TreeRow &row, float indent_spacing) {
  const float base = static_cast<float>(row.depth) * indent_spacing;
  if (row.is_directory) {
    return base;
  }
  if (row.depth == 0) {
    return indent_spacing;
  }
  return base + ui::scale::px(kDepthIndent) * static_cast<float>(row.depth);
}

void render_directory_row(PatchSelectorContext &context, const TreeRow &row,
                          PatchTreeCache &cache) {
  const auto &item = *row.entry;

  ImGui::PushID(item.relative_path.c_str());
  std::string display_name = item.name;
  if (item.relative_path == kBuiltinPresetRoot) {
    display_name = std::string(kBuiltinPresetDisplayName);
  }

  // The row list owns the open state, so the node is driven rather than left
  // to ImGui's storage; a toggle is picked up from the returned value.
  ImGui::SetNextItemOpen(row.is_open, ImGuiCond_Always);
  const bool open = ImGui::TreeNodeEx(display_name.c_str(),
                                      ImGuiTreeNodeFlags_SpanFullWidth |
                                          ImGuiTreeNodeFlags_NoTreePushOnOpen);
  entry_context_menu(context, item,
                     row.depth == 0 &&
                         item.relative_path != kBuiltinPresetRoot);
  entry_drag_source(context, item, row.depth);
  folder_drop_target(context, item);
  handle_row_keys(context, item);
  if (open != row.is_open) {
    cache.set_open(item.relative_path, open);
  }
  ImGui::PopID();
}

void render_patch_row(PatchSelectorContext &context, const TreeRow &row) {
  const auto &item = *row.entry;

  ImGui::PushID(item.relative_path.c_str());

  const bool is_current =
      item.relative_path == context.session.current_patch_selection_path();
  if (is_current) {
    ImGui::PushStyleColor(ImGuiCol_Text,
                          styles::color(styles::MegatoyCol::TextHighlight));
  }

  // Name and dimmed format label, either of which loads the patch. The
  // context menu and tooltip attach to whichever was drawn last, so both
  // are registered after each selectable.
  const bool name_clicked = ImGui::Selectable(item.name.c_str(), false);
  if (is_current) {
    ImGui::PopStyleColor();
  }
  entry_context_menu(context, item);
  show_patch_tooltip(item);
  entry_drag_source(context, item, row.depth);
  handle_row_keys(context, item);

  ImGui::SameLine();
  const ImVec4 format_color =
      is_current ? styles::color(styles::MegatoyCol::TextHighlight)
                 : ImGui::GetStyleColorVec4(ImGuiCol_Text);
  ImGui::PushStyleColor(ImGuiCol_Text,
                        color_with_alpha_vec4(format_color, 0.5f));
  const bool format_clicked = ImGui::Selectable(item.format.c_str(), false);
  ImGui::PopStyleColor();
  entry_context_menu(context, item);
  show_patch_tooltip(item);
  entry_drag_source(context, item, row.depth);
  handle_row_keys(context, item);

  if ((name_clicked || format_clicked) && context.safe_load_patch) {
    context.safe_load_patch(item);
  }

  ImGui::PopID();
}

} // namespace

namespace {

/// One tree is on screen, so one cache: the context menu reaches it to open
/// the folder a new one is made in.
PatchTreeCache &tree_cache() {
  static PatchTreeCache cache;
  return cache;
}

} // namespace

void open_tree_directory(const std::string &relative_path) {
  tree_cache().set_open(relative_path, true);
}

void follow_tree_directory(const std::string &from, const std::string &to) {
  tree_cache().follow(from, to);
}

bool render_patch_tree(const std::vector<patches::PatchEntry> &tree,
                       PatchSelectorContext &context,
                       const std::string &query_lower, int min_star_rating) {
  PatchTreeCache &cache = tree_cache();
  const auto &rows = cache.get(tree, context.repository.revision(), query_lower,
                               min_star_rating);
  if (rows.empty()) {
    return false;
  }

  // Every row is exactly one text line high, which is what lets the clipper
  // skip the rows outside the view.
  const float indent_spacing = ImGui::GetStyle().IndentSpacing;
  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(rows.size()));
  while (clipper.Step()) {
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
      const auto &row = rows[static_cast<std::size_t>(i)];
      const float offset = row_indent(row, indent_spacing);
      if (offset > 0.0f) {
        ImGui::Indent(offset);
      }
      if (row.is_directory) {
        render_directory_row(context, row, cache);
      } else {
        render_patch_row(context, row);
      }
      if (offset > 0.0f) {
        ImGui::Unindent(offset);
      }
    }
  }

  return true;
}

} // namespace ui::selector_detail
