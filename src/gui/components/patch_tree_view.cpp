#include "patch_tree_view.hpp"

#include "common.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include "patch_selector_shared.hpp"
#include "patch_tree_flatten.hpp"
#include "star_rating.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <imgui.h>
#include <imgui_internal.h>
#include <optional>
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

/// Lets the last drawn item be dragged: any patch, to wherever takes one, and
/// a folder that can be renamed, onto another folder. Workspace folders stay
/// put.
void entry_drag_source(const PatchSelectorContext &context,
                       const patches::PatchEntry &entry, int depth) {
  if (depth == 0 || !ImGui::IsItemActive()) {
    return;
  }
  if (entry.is_directory &&
      (!context.move_patch || !entry_can_rename(context, entry))) {
    return;
  }
  if (ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload(kPatchEntryPayload, entry.relative_path.data(),
                              entry.relative_path.size());
    ImGui::TextUnformatted(entry.name.c_str());
    ImGui::EndDragDropSource();
  }
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
 * How far a row is indented. Files step in one IndentSpacing per level plus
 * kDepthIndent. A folder's name lines up with the files of its level, its
 * arrow standing out to the left of them.
 */
float row_indent(const TreeRow &row, float indent_spacing) {
  const float depth = static_cast<float>(row.depth);
  const float file_indent =
      row.depth == 0
          ? indent_spacing
          : depth * indent_spacing + ui::scale::px(kDepthIndent) * depth;
  if (!row.is_directory) {
    return file_indent;
  }
  return std::max(0.0f, file_indent - ImGui::GetTreeNodeToLabelSpacing());
}

/// While filtering, every directory leading to a match is shown open, and a
/// toggle is not kept: what was open before comes back with the filter off.
void render_directory_row(PatchSelectorContext &context, const TreeRow &row,
                          PatchTreeCache &cache, bool filtering) {
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
  handle_row_keys(context, item);
  if (open != row.is_open && !filtering) {
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
  const bool name_clicked = ImGui::Selectable(
      item.name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
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
  const bool format_clicked = ImGui::Selectable(
      item.format.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
  ImGui::PopStyleColor();
  entry_context_menu(context, item);
  show_patch_tooltip(item);
  entry_drag_source(context, item, row.depth);
  handle_row_keys(context, item);

  if (const int stars = item.metadata ? item.metadata->star_rating : 0;
      stars > 0) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", kStarLabels[static_cast<std::size_t>(
                                  std::min(stars, 5))]
                                  .data());
  }

  // A click previews the patch, a double click keeps it open.
  if ((name_clicked || format_clicked) && context.open_patch) {
    context.open_patch(item,
                       ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left));
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

/// The folder a drop on row `index` goes into: the row itself when it is a
/// folder, otherwise the folder it is listed under. npos at the top level.
std::size_t drop_folder_index(const std::vector<TreeRow> &rows,
                              std::size_t index) {
  if (rows[index].is_directory) {
    return index;
  }
  for (std::size_t i = index; i-- > 0;) {
    if (rows[i].depth < rows[index].depth) {
      return i;
    }
  }
  return std::string::npos;
}

/// The last row listed under the folder on row `index`, or the row itself.
std::size_t last_row_under(const std::vector<TreeRow> &rows,
                           std::size_t index) {
  std::size_t last = index;
  while (last + 1 < rows.size() && rows[last + 1].depth > rows[index].depth) {
    ++last;
  }
  return last;
}

/// A folder an entry is held over, and where the row under the mouse is.
struct DropHover {
  std::size_t folder = 0;
  std::size_t row = 0;
  float row_top = 0.0f;
};

/**
 * Makes row `index`, across the whole width, take an entry for its folder.
 * True with `hover` filled while one that folder can take is held over it.
 */
bool row_drop_target(PatchSelectorContext &context,
                     const std::vector<TreeRow> &rows, std::size_t index,
                     const ImRect &rect, DropHover &hover) {
  const std::size_t folder_index = drop_folder_index(rows, index);
  if (folder_index == std::string::npos) {
    return false;
  }
  const ImGuiPayload *pending = ImGui::GetDragDropPayload();
  if (pending == nullptr || !pending->IsDataType(kPatchEntryPayload)) {
    return false;
  }
  const std::string relative_path(static_cast<const char *>(pending->Data),
                                  static_cast<std::size_t>(pending->DataSize));
  const auto *source =
      find_by_relative_path(context.repository.tree(), relative_path);
  const auto &folder = *rows[folder_index].entry;
  if (source == nullptr || !context.session.can_move_patch(*source, folder)) {
    return false;
  }
  if (!ImGui::BeginDragDropTargetCustom(
          rect, ImGui::GetID(rows[index].entry->relative_path.c_str()))) {
    return false;
  }
  bool held = false;
  if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(
          kPatchEntryPayload, ImGuiDragDropFlags_AcceptBeforeDelivery |
                                  ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
    if (payload->IsPreview()) {
      hover = {folder_index, index, rect.Min.y};
      held = true;
    }
    if (payload->IsDelivery()) {
      context.pending_move = PendingMove{*source, folder};
    }
  }
  ImGui::EndDragDropTarget();
  return held;
}

/// The frame around the folder taking a drop and everything listed under it,
/// as four strips so its edges stay sharp.
void draw_drop_frame(const ImVec2 &min, const ImVec2 &max) {
  const float t = ImGui::GetStyle().DragDropTargetBorderSize;
  const ImU32 color = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  draw_list->AddRectFilled(min, ImVec2(max.x, min.y + t), color);
  draw_list->AddRectFilled(ImVec2(min.x, max.y - t), max, color);
  draw_list->AddRectFilled(ImVec2(min.x, min.y + t),
                           ImVec2(min.x + t, max.y - t), color);
  draw_list->AddRectFilled(ImVec2(max.x - t, min.y + t),
                           ImVec2(max.x, max.y - t), color);
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
  const float left = ImGui::GetCursorScreenPos().x;
  const float right = ImGui::GetCurrentWindow()->WorkRect.Max.x;
  const bool can_drop = static_cast<bool>(context.move_patch);
  std::optional<DropHover> hover;
  float row_height = 0.0f;
  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(rows.size()));
  while (clipper.Step()) {
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
      const auto index = static_cast<std::size_t>(i);
      const auto &row = rows[index];
      const float top = ImGui::GetCursorScreenPos().y;
      const float offset = row_indent(row, indent_spacing);
      if (offset > 0.0f) {
        ImGui::Indent(offset);
      }
      if (row.is_directory) {
        render_directory_row(context, row, cache,
                             !query_lower.empty() || min_star_rating > 0);
      } else {
        render_patch_row(context, row);
      }
      if (offset > 0.0f) {
        ImGui::Unindent(offset);
      }
      row_height = ImGui::GetCursorScreenPos().y - top;
      DropHover held;
      if (can_drop &&
          row_drop_target(context, rows, index,
                          ImRect(left, top, right, top + row_height), held)) {
        hover = held;
      }
    }
  }

  // Every row is the same height, so the folder's rows are found from the
  // one under the mouse even where the clipper skipped them.
  if (hover) {
    const float folder_top =
        hover->row_top -
        static_cast<float>(hover->row - hover->folder) * row_height;
    const float bottom =
        hover->row_top +
        static_cast<float>(last_row_under(rows, hover->folder) - hover->row +
                           1) *
            row_height;
    draw_drop_frame(
        ImVec2(left + row_indent(rows[hover->folder], indent_spacing),
               folder_top),
        ImVec2(right, bottom));
  }

  return true;
}

} // namespace ui::selector_detail
