#include "patch_tree_view.hpp"
#include "gui/components/instrument_icon_picker.hpp"
#include "gui/components/menu_rows.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "patches/instrument_icons.hpp"

#include "common.hpp"
#include "gui/ui_scale.hpp"
#include "patch_selector_shared.hpp"
#include "patches/patch_tree_flatten.hpp"
#include "patches/patch_tree_navigation.hpp"
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

/// How far each level's names sit in from the level above.
constexpr float kDepthIndent = 10.0f;

/**
 * The visible rows, rebuilt only when the tree, the filters or the expansion
 * state change. Expansion lives here too, because ImGui's own tree state is
 * unreachable for rows out of view, which are never drawn.
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
  std::vector<patches::TreeRow> rows;

  patches::TreeRevealState reveal_state;

  const std::vector<patches::TreeRow> &
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
      rows = patches::flatten_visible_rows(source, query_lower, min_star_rating,
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

  /**
   * Opens the directories above the current patch once it has become current.
   * True when the view should be brought to it. While filtering the rows
   * already follow the matches, and what was open is left as it was.
   */
  bool reveal(const std::vector<patches::PatchEntry> &source,
              const std::string &selection, std::uint64_t revision,
              bool filtering) {
    const auto above = patches::reveal_tree_selection(
        reveal_state, source, selection, revision, filtering);
    if (!above) {
      return false;
    }
    for (const auto &directory : *above) {
      set_open(directory, true);
    }
    return true;
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

/// A row's height: a text line and the margin around it, all of which the row
/// answers the mouse in. Rows are set with no spacing between them, so their
/// highlights meet.
float row_margin() { return ui::list_row_spacing(); }

float row_height() { return ImGui::GetTextLineHeight() + row_margin(); }

/// How much larger than the text a patch's instrument icon is drawn.
constexpr float kPatchIconScale = 1.5f;

/// A patch's row: a folder's and more above and below, for its larger icon.
float patch_row_height() { return row_height() + ui::scale::px(8.0f); }

float row_height_of(const patches::TreeRow &row) {
  return row.is_directory ? row_height() : patch_row_height();
}

/// How wide a patch's icon is drawn.
float patch_icon_width() {
  return std::round(ImGui::GetFontSize() * kPatchIconScale);
}

/// How far a name starts past a patch's icon or a folder's arrow, the two
/// standing at one place: the icon's width and the frame padding after it.
float name_offset() {
  return patch_icon_width() + ImGui::GetStyle().FramePadding.x;
}

/// Text after what is already on the row, `drop` below the row's top. A folder
/// row's text is already placed by its frame padding, so it takes none.
void row_trailing_text(const char *text, const ImVec4 &color, float drop) {
  ImGui::SameLine();
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + drop);
  ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, 0.0f);
  ImGui::TextColored(color, "%s", text);
  ImGui::PopStyleVar();
}

bool row_selectable(const char *label, float height, bool selected = false) {
  ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
  const bool clicked =
      ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowDoubleClick,
                        ImVec2(0.0f, height));
  ImGui::PopStyleVar(2);
  return clicked;
}

/// A patch's instrument icon, larger than the text, and its name, over a
/// selectable that spans the row.
bool patch_name_selectable(const patches::PatchEntry &item, bool selected) {
  const std::string_view icon_id = item.metadata
                                       ? std::string_view(item.metadata->icon)
                                       : std::string_view();
  const float height = patch_row_height();
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  // Sized to the icon and name, which the next item follows, while the
  // highlight still spans the row.
  ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowDoubleClick;
  flags |= ImGuiSelectableFlags_SpanAvailWidth;
  ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, 0.0f);
  const bool clicked = ImGui::Selectable(
      "##name", selected, flags,
      ImVec2(name_offset() + ImGui::CalcTextSize(item.name.c_str()).x, height));
  ImGui::PopStyleVar();
  // Centred under a folder's arrow, which stands a text height wide; the
  // larger icon reaches out to either side of it.
  const float icon_left =
      pos.x - (patch_icon_width() - ImGui::GetFontSize()) * 0.5f;
  draw_instrument_icon(icon_id, ImVec2(icon_left, pos.y),
                       ImVec2(icon_left + patch_icon_width(), pos.y + height),
                       kPatchIconScale);
  ImGui::GetWindowDrawList()->AddText(
      ImVec2(pos.x + name_offset(),
             pos.y + (height - ImGui::GetTextLineHeight()) * 0.5f),
      ImGui::GetColorU32(ImGuiCol_Text), item.name.c_str());
  return clicked;
}

/// Whether a row heads one of the top folders, whose contents stand below it
/// without indent.
bool is_heading(const patches::TreeRow &row) {
  return row.is_directory && row.depth == 0;
}

/**
 * How far a row is indented. The rows under a top folder's heading start at
 * its left edge, and every level below them kDepthIndent in from the level
 * above. A folder's arrow sits the frame padding in from where its row
 * starts, and a patch's icon is set in as far, so the two line up.
 */
float row_indent(const patches::TreeRow &row) {
  const int level = std::max(row.depth - 1, 0);
  const float indent = ui::scale::px(kDepthIndent) * static_cast<float>(level);
  return row.is_directory ? indent : indent + ImGui::GetStyle().FramePadding.x;
}

/// While filtering, every directory leading to a match is shown open, and a
/// toggle is not kept: what was open before comes back with the filter off.
void render_directory_row(PatchSelectorContext &context,
                          const patches::TreeRow &row, PatchTreeCache &cache,
                          bool filtering) {
  const auto &item = *row.entry;

  ImGui::PushID(item.relative_path.c_str());
  std::string display_name = item.name;
  if (item.relative_path == kBuiltinPresetRoot) {
    display_name = std::string(kBuiltinPresetDisplayName);
  }

  // The row list owns the open state, so the node is driven rather than left
  // to ImGui's storage; a toggle is picked up from the returned value.
  ImGui::SetNextItemOpen(row.is_open, ImGuiCond_Always);
  ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, 0.0f);
  ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, row_margin() * 0.5f);
  // Folders are muted beside the patches they hold; a top folder's heading
  // is a bar, as an editor section's is, drawn behind the node.
  const bool heading = is_heading(row);
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  if (heading) {
    draw_list->ChannelsSplit(2);
    draw_list->ChannelsSetCurrent(1);
    const ImVec4 none(0.0f, 0.0f, 0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, none);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, none);
  } else {
    ImGui::PushStyleColor(ImGuiCol_Text,
                          styles::color(styles::MegatoyCol::TextMuted));
  }
  // A folder's name stands where a patch's does past its larger icon, so it
  // is drawn here rather than by the node.
  const ImVec2 row_min = ImGui::GetCursorScreenPos();
  const bool open = ImGui::TreeNodeEx(
      heading ? display_name.c_str() : "##folder",
      ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding |
          ImGuiTreeNodeFlags_NoTreePushOnOpen);
  if (!heading) {
    const ImU32 muted = ImGui::GetColorU32(ImGuiCol_Text);
    const float text_y =
        row_min.y + (row_height() - ImGui::GetTextLineHeight()) * 0.5f;
    const float name_x =
        row_min.x + ImGui::GetStyle().FramePadding.x + name_offset();
    draw_list->AddText(ImVec2(name_x, text_y), muted, display_name.c_str());
    if (!item.telling_parents.empty()) {
      draw_list->AddText(
          ImVec2(name_x + ImGui::CalcTextSize(display_name.c_str()).x +
                     ImGui::GetStyle().ItemSpacing.x,
                 text_y),
          muted, item.telling_parents.c_str());
    }
  }
  ImGui::PopStyleColor(heading ? 2 : 1);
  if (heading) {
    using styles::MegatoyCol;
    const auto col = ImGui::IsItemActive()    ? MegatoyCol::SectionHeaderActive
                     : ImGui::IsItemHovered() ? MegatoyCol::SectionHeaderHovered
                                              : MegatoyCol::SectionHeader;
    draw_list->ChannelsSetCurrent(0);
    draw_list->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                             styles::color_u32(col),
                             ImGui::GetStyle().FrameRounding);
    draw_list->ChannelsMerge();
  }
  ImGui::PopStyleVar(2);
  entry_context_menu(context, item,
                     row.depth == 0 &&
                         item.relative_path != kBuiltinPresetRoot);
  entry_drag_source(context, item, row.depth);
  handle_row_keys(context, item);
  if (open != row.is_open && !filtering) {
    cache.set_open(item.relative_path, open);
  }
  if (heading && !item.telling_parents.empty()) {
    row_trailing_text(item.telling_parents.c_str(),
                      styles::color(styles::MegatoyCol::TextMuted), 0.0f);
  }
  ImGui::PopID();
}

void render_patch_row(PatchSelectorContext &context,
                      const patches::TreeRow &row) {
  const auto &item = *row.entry;

  ImGui::PushID(item.relative_path.c_str());

  const bool is_current =
      item.relative_path == context.session.current_patch_selection_path();

  // Name and dimmed format label, either of which loads the patch. The
  // context menu and tooltip attach to whichever was drawn last, so both
  // are registered after each selectable. The name's selectable spans the
  // row, so it alone carries the current patch's highlight.
  const bool name_clicked = patch_name_selectable(item, is_current);
  entry_context_menu(context, item);
  show_patch_tooltip(context.repository, item);
  entry_drag_source(context, item, row.depth);
  handle_row_keys(context, item);

  ImGui::SameLine();
  ImGui::PushStyleColor(ImGuiCol_Text,
                        styles::color(styles::MegatoyCol::TextMuted));
  const bool format_clicked =
      row_selectable(item.format.c_str(), patch_row_height());
  ImGui::PopStyleColor();
  entry_context_menu(context, item);
  show_patch_tooltip(context.repository, item);
  entry_drag_source(context, item, row.depth);
  handle_row_keys(context, item);

  if (const int stars = item.metadata ? item.metadata->star_rating : 0;
      stars > 0) {
    row_trailing_text(
        kStarLabels[static_cast<std::size_t>(std::min(stars, 5))].data(),
        styles::color(styles::MegatoyCol::TextMuted),
        (patch_row_height() - ImGui::GetTextLineHeight()) * 0.5f);
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

/// The folder an entry is held over.
struct DropHover {
  std::size_t folder = 0;
};

/**
 * Makes row `index`, across the whole width, take an entry for its folder.
 * True with `hover` filled while one that folder can take is held over it.
 */
bool row_drop_target(PatchSelectorContext &context,
                     const std::vector<patches::TreeRow> &rows,
                     std::size_t index, const ImRect &rect, DropHover &hover) {
  const std::size_t folder_index = patches::drop_folder_index(rows, index);
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
      hover = {folder_index};
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

/// Scrolls the row at `row_top` into view: only as far as it takes for a row
/// partly in view, otherwise to the middle.
void scroll_to_row(float row_top, float height) {
  if (const auto scroll = patches::scroll_for_row(
          0, row_top, height, ImGui::GetWindowHeight(), ImGui::GetScrollY())) {
    ImGui::SetScrollY(*scroll);
  }
}

/**
 * Where each row starts below the list's top, and last where the list ends.
 * A top folder's heading after other rows stands a gap below them, as an
 * editor section does below the one before.
 */
std::vector<float> row_tops(const std::vector<patches::TreeRow> &rows) {
  const float gap = ImGui::GetStyle().ItemSpacing.y;
  std::vector<float> tops;
  tops.reserve(rows.size() + 1);
  float y = 0.0f;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (i > 0 && is_heading(rows[i])) {
      y += gap;
    }
    tops.push_back(y);
    y += row_height_of(rows[i]);
  }
  tops.push_back(y);
  return tops;
}

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
  const auto &selection = context.session.current_patch_selection_path();
  const bool reveal =
      cache.reveal(tree, selection, context.repository.revision(),
                   !query_lower.empty() || min_star_rating > 0);
  const auto &rows = cache.get(tree, context.repository.revision(), query_lower,
                               min_star_rating);
  if (rows.empty()) {
    return false;
  }

  const std::vector<float> tops = row_tops(rows);
  if (reveal) {
    const auto row = std::find_if(rows.begin(), rows.end(), [&](const auto &r) {
      return r.entry->relative_path == selection;
    });
    if (row != rows.end()) {
      scroll_to_row(ImGui::GetCursorPosY() +
                        tops[static_cast<std::size_t>(row - rows.begin())],
                    row_height_of(*row));
    }
  }

  // Only the rows in view are drawn, found from where each one starts.
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float left = origin.x;
  const float right = ImGui::GetCurrentWindow()->WorkRect.Max.x;
  const ImRect clip = ImGui::GetCurrentWindow()->ClipRect;
  // The tallest a row stands, to find the first one in view.
  const float height = patch_row_height();
  const bool can_drop = static_cast<bool>(context.move_patch);
  std::optional<DropHover> hover;
  const auto first = static_cast<std::size_t>(
      std::upper_bound(tops.begin(), tops.end() - 1,
                       clip.Min.y - origin.y - height) -
      tops.begin());
  for (std::size_t index = first;
       index < rows.size() && origin.y + tops[index] < clip.Max.y; ++index) {
    const auto &row = rows[index];
    const float top = origin.y + tops[index];
    ImGui::SetCursorScreenPos(ImVec2(left, top));
    const float offset = row_indent(row);
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
    DropHover held;
    if (can_drop &&
        row_drop_target(context, rows, index,
                        ImRect(left, top, right, top + row_height_of(row)),
                        held)) {
      hover = held;
    }
  }
  ImGui::SetCursorScreenPos(ImVec2(left, origin.y + tops.back()));
  ImGui::Dummy(ImVec2(0.0f, 0.0f));

  // The folder taking a drop is framed with everything listed under it,
  // even the rows out of view.
  if (hover) {
    const std::size_t last = patches::last_row_under(rows, hover->folder);
    draw_drop_frame(
        ImVec2(left + row_indent(rows[hover->folder]),
               origin.y + tops[hover->folder]),
        ImVec2(right, origin.y + tops[last] + row_height_of(rows[last])));
  }

  return true;
}

} // namespace ui::selector_detail
