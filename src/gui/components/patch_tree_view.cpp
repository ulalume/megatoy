#include "patch_tree_view.hpp"
#include "gui/components/centred_glyph.hpp"
#include "gui/components/instrument_icon_picker.hpp"
#include "gui/components/menu_rows.hpp"
#include "gui/components/section_header.hpp"
#include "gui/fonts/megatoy_icons.hpp"
#include "gui/os_file_drop.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "patches/instrument_icons.hpp"

#include "common.hpp"
#include "gui/ui_scale.hpp"
#include "patch_selector_shared.hpp"
#include "patches/patch_tree_flatten.hpp"
#include "patches/patch_tree_navigation.hpp"
#include "platform/platform_config.hpp"
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
  patches::StarFilter star_filter;
  std::uint64_t built_open_revision = 0;

  /// Bumped on every expand/collapse, so the rows rebuild on the next frame.
  std::uint64_t open_revision = 1;
  std::unordered_set<std::string> open_directories;
  std::vector<patches::TreeRow> rows;
  /// Bumped whenever `rows` is rebuilt.
  std::uint64_t rows_build = 0;
  /// Where each row starts, kept while the rows and the sizes they were laid
  /// out with stay.
  std::vector<float> tops;
  std::uint64_t tops_build = 0;
  float tops_line = 0.0f;
  float tops_gap = 0.0f;

  patches::TreeRevealState reveal_state;

  const std::vector<patches::TreeRow> &
  get(const std::vector<patches::PatchEntry> &source, std::uint64_t revision,
      const std::string &query_lower, patches::StarFilter shown_stars) {
    if (tree != &source || repository_revision != revision ||
        search_query != query_lower || star_filter != shown_stars ||
        built_open_revision != open_revision) {
      tree = &source;
      repository_revision = revision;
      search_query = query_lower;
      star_filter = shown_stars;
      built_open_revision = open_revision;
      // While filtering, the patches that match stand in one list, out of
      // their folders.
      rows =
          query_lower.empty() && !shown_stars.active()
              ? patches::flatten_visible_rows(source, open_directories)
              : patches::matching_patch_rows(source, query_lower, shown_stars);
      ++rows_build;
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

/// What dropping the entry being dragged does where it is held, as the
/// folder under it found last frame.
enum class DropAction { None, Move, Copy };
DropAction g_held_drop = DropAction::None;

/**
 * What dropping `source` into `folder` does. An entry megatoy can move goes
 * there, or is copied while Option/Alt is held; one it cannot, from the
 * built-in presets, a read-only folder or inside a bank, is always copied.
 * Moves stay inside one workspace folder, so into another it is copied too,
 * but back into its own folder it is only with Option/Alt.
 */
DropAction drop_action(const PatchSelectorContext &context,
                       const patches::PatchEntry &source,
                       const patches::PatchEntry &folder) {
  const bool movable = context.move_patch && entry_can_rename(context, source);
  if (movable && !ImGui::GetIO().KeyAlt) {
    if (context.session.can_move_patch(source, folder)) {
      return DropAction::Move;
    }
    if (source.full_path.parent_path() == folder.full_path) {
      return DropAction::None;
    }
  }
  return context.copy_patch && context.repository.can_copy_patch(source, folder)
             ? DropAction::Copy
             : DropAction::None;
}

/// Lets the last drawn item be dragged onto a folder, which moves or copies
/// it (see drop_action). Workspace folders stay put. Only a folder that takes
/// the entry answers it, opening when it is held over (see spring_open), so
/// ImGui's own hold-to-open is off.
void entry_drag_source(const patches::PatchEntry &entry, int depth) {
  if (depth == 0 || !ImGui::IsItemActive()) {
    return;
  }
  if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip |
                                 ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
    ImGui::SetDragDropPayload(kPatchEntryPayload, entry.relative_path.data(),
                              entry.relative_path.size());
    ImGui::EndDragDropSource();
  }
}

/**
 * What follows the pointer while an entry is dragged. Drawn here rather than
 * by its row, which is not drawn while out of view, and ImGui would show
 * "..." in its place.
 */
void draw_drag_tooltip(const PatchSelectorContext &context) {
  const ImGuiPayload *payload = ImGui::GetDragDropPayload();
  if (payload == nullptr || !payload->IsDataType(kPatchEntryPayload)) {
    return;
  }
  const std::string relative_path(static_cast<const char *>(payload->Data),
                                  static_cast<std::size_t>(payload->DataSize));
  const auto *entry =
      find_by_relative_path(context.repository.tree(), relative_path);
  if (entry == nullptr) {
    return;
  }
  // Placed as ImGui places a drag source's own tooltip, close to the pointer.
  ImGuiContext &g = *GImGui;
  g.DragDropWithinSource = true;
  ImGui::BeginTooltip();
  if (g_held_drop == DropAction::Copy) {
    ImGui::TextUnformatted(ICON_MEGATOY_PLUS);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
  }
  ImGui::TextUnformatted(entry->name.c_str());
  if (context.move_patch && entry_can_rename(context, *entry)) {
    ImGui::TextDisabled(ImGui::GetIO().ConfigMacOSXBehaviors ? "Option to copy"
                                                             : "Alt to copy");
  }
  ImGui::EndTooltip();
  g.DragDropWithinSource = false;
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

float row_height_of(const patches::TreeRow &row) {
  return row.is_directory ? row_height() : ui::patch_row_height();
}

/// How far a name starts past a patch's icon or a folder's arrow, the two
/// standing at one place: the icon's width and the frame padding after it.
float name_offset() {
  return ui::patch_row_icon_width() + ImGui::GetStyle().FramePadding.x;
}

/// How much larger than the text the star of a rating too wide for its row
/// is drawn, with the count inside it.
constexpr float kRatingBadgeScale = 1.4f;
/// The count's size inside that star, against the text's.
constexpr float kRatingBadgeDigitScale = 0.7f;

/// The width a patch's rating takes on its row: its stars, or the one star
/// with the count in it, by `compact`.
float rating_width(int stars, bool compact) {
  if (compact) {
    return std::round(ImGui::GetFontSize() * kRatingBadgeScale);
  }
  return ImGui::CalcTextSize(
             kStarLabels[static_cast<std::size_t>(stars)].data())
      .x;
}

/// A patch's rating, its right end at `right`, centred on `centre_y`.
void draw_rating(int stars, bool compact, float right, float centre_y) {
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  const ImU32 muted =
      ImGui::GetColorU32(styles::color(styles::MegatoyCol::TextMuted));
  const float width = rating_width(stars, compact);
  if (!compact) {
    draw_list->AddText(
        ImVec2(right - width, centre_y - ImGui::GetTextLineHeight() * 0.5f),
        muted, kStarLabels[static_cast<std::size_t>(stars)].data());
    return;
  }
  const ImVec2 min(right - width, centre_y - width * 0.5f);
  const ImVec2 max(right, centre_y + width * 0.5f);
  draw_centred_glyph(ICON_MEGATOY_STAR, min, max, muted, kRatingBadgeScale);
  // The count in the background's colour, a little low: a star's middle
  // stands below the middle of its outline.
  const char digit[2] = {static_cast<char>('0' + stars), '\0'};
  const float drop = width * 0.06f;
  draw_centred_glyph(
      digit, ImVec2(min.x, min.y + drop), ImVec2(max.x, max.y + drop),
      ImGui::GetColorU32(ImGuiCol_WindowBg), kRatingBadgeDigitScale);
}

/**
 * A patch's row, one selectable spanning it: the instrument icon, larger
 * than the text, the name and the dimmed format, and the rating at the
 * right end. A rating whose stars leave the name and format no room is one
 * star with the count in it, and a name that still does not fit is cut
 * short.
 */
bool patch_row_selectable(const patches::PatchEntry &item, bool selected) {
  const std::string_view icon_id = item.metadata
                                       ? std::string_view(item.metadata->icon)
                                       : std::string_view();
  const int stars =
      std::clamp(item.metadata ? item.metadata->star_rating : 0, 0, 5);
  const ImGuiStyle &style = ImGui::GetStyle();
  const float height = ui::patch_row_height();
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float right =
      pos.x + ImGui::GetContentRegionAvail().x - style.FramePadding.x;
  // The highlight reaches out to the left as far as a folder row's does
  // past the padding, the contents staying where they are.
  const ImGuiWindow *window = ImGui::GetCurrentWindow();
  ImGui::SetCursorScreenPos(
      ImVec2(pos.x - (window->WindowPadding.x - ui::scale::px(1.0f)), pos.y));
  ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, 0.0f);
  const bool clicked =
      ImGui::Selectable("##patch", selected,
                        ImGuiSelectableFlags_AllowDoubleClick |
                            ImGuiSelectableFlags_SpanAvailWidth,
                        ImVec2(0.0f, height));
  ImGui::PopStyleVar();

  // Centred under a folder's arrow, which stands a text height wide; the
  // larger icon reaches out to either side of it.
  const float icon_left =
      pos.x - (ui::patch_row_icon_width() - ImGui::GetFontSize()) * 0.5f;
  draw_instrument_icon(
      icon_id, ImVec2(icon_left, pos.y),
      ImVec2(icon_left + ui::patch_row_icon_width(), pos.y + height),
      ui::kPatchRowIconScale);

  const float centre_y = pos.y + height * 0.5f;
  const float text_y = centre_y - ImGui::GetTextLineHeight() * 0.5f;
  const float name_x = pos.x + name_offset();
  const float gap = style.ItemSpacing.x;
  const ImVec2 name_size = ImGui::CalcTextSize(item.name.c_str());
  const float format_width = ImGui::CalcTextSize(item.format.c_str()).x;
  const float text_end = name_x + name_size.x + gap + format_width;
  bool compact = false;
  float limit = right;
  if (stars > 0) {
    compact = text_end + gap + rating_width(stars, false) > right;
    limit = right - rating_width(stars, compact) - gap;
    draw_rating(stars, compact, right, centre_y);
  }

  // The format keeps its place after the name, which gives way to it.
  const float name_room = std::max(limit - gap - format_width - name_x, 0.0f);
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  if (name_size.x <= name_room) {
    draw_list->AddText(ImVec2(name_x, text_y),
                       ImGui::GetColorU32(ImGuiCol_Text), item.name.c_str());
  } else {
    ImGui::RenderTextEllipsis(draw_list, ImVec2(name_x, text_y),
                              ImVec2(name_x + name_room, text_y + name_size.y),
                              name_x + name_room, item.name.c_str(), nullptr,
                              &name_size);
  }
  const float format_x = name_x + std::min(name_size.x, name_room) + gap;
  draw_list->PushClipRect(ImVec2(format_x, pos.y),
                          ImVec2(limit, pos.y + height), true);
  draw_list->AddText(
      ImVec2(format_x, text_y),
      ImGui::GetColorU32(styles::color(styles::MegatoyCol::TextMuted)),
      item.format.c_str());
  draw_list->PopClipRect();
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

void render_directory_row(PatchSelectorContext &context,
                          const patches::TreeRow &row, PatchTreeCache &cache) {
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
    // The node's own arrow is left unseen: a heading shows a folder, open or
    // closed, in its place.
    const ImVec4 none(0.0f, 0.0f, 0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, none);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, none);
    ImGui::PushStyleColor(ImGuiCol_Text, none);
  } else {
    ImGui::PushStyleColor(ImGuiCol_Text,
                          styles::color(styles::MegatoyCol::TextMuted));
  }
  // A folder's name stands where a patch's does past its larger icon, so it
  // is drawn here rather than by the node.
  const ImVec2 row_min = ImGui::GetCursorScreenPos();
  const bool open =
      ImGui::TreeNodeEx("##folder", ImGuiTreeNodeFlags_SpanFullWidth |
                                        ImGuiTreeNodeFlags_FramePadding |
                                        ImGuiTreeNodeFlags_NoTreePushOnOpen);
  ImGui::PopStyleColor(heading ? 3 : 1);
  {
    const ImGuiStyle &style = ImGui::GetStyle();
    const float text_y =
        row_min.y + (row_height() - ImGui::GetTextLineHeight()) * 0.5f;
    const ImU32 color =
        heading
            ? ImGui::GetColorU32(ImGuiCol_Text)
            : ImGui::GetColorU32(styles::color(styles::MegatoyCol::TextMuted));
    // Every row's name stands at the same distance past its arrow or icon.
    const float name_x = row_min.x + style.FramePadding.x + name_offset();
    if (heading) {
      // Where a node's arrow stands.
      const ImVec2 icon_min(row_min.x + style.FramePadding.x, row_min.y);
      draw_centred_glyph(
          row.is_open ? ICON_MEGATOY_FOLDER_OPEN : ICON_MEGATOY_FOLDER,
          icon_min,
          ImVec2(icon_min.x + ImGui::GetFontSize(), row_min.y + row_height()),
          color);
    }
    draw_list->AddText(ImVec2(name_x, text_y), color, display_name.c_str());
    if (!item.telling_parents.empty()) {
      draw_list->AddText(
          ImVec2(name_x + ImGui::CalcTextSize(display_name.c_str()).x +
                     style.ItemSpacing.x,
                 text_y),
          ImGui::GetColorU32(styles::color(styles::MegatoyCol::TextMuted)),
          item.telling_parents.c_str());
    }
  }
  if (heading) {
    draw_list->ChannelsSetCurrent(0);
    draw_section_header_bar(draw_list, ImGui::GetItemRectMin(),
                            ImGui::GetItemRectMax());
    draw_list->ChannelsMerge();
  }
  // On the desktop a top folder is a place on disk, named in full.
  if (heading && !megatoy::platform::is_web() &&
      ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
    ImGui::SetTooltip("%s", item.full_path.string().c_str());
  }
  ImGui::PopStyleVar(2);
  entry_context_menu(context, item,
                     row.depth == 0 &&
                         item.relative_path != kBuiltinPresetRoot);
  entry_drag_source(item, row.depth);
  handle_row_keys(context, item);
  if (open != row.is_open) {
    cache.set_open(item.relative_path, open);
  }
  ImGui::PopID();
}

void render_patch_row(PatchSelectorContext &context,
                      const patches::TreeRow &row) {
  const auto &item = *row.entry;

  ImGui::PushID(item.relative_path.c_str());

  const bool is_current =
      item.relative_path == context.session.current_patch_selection_path();

  const bool clicked = patch_row_selectable(item, is_current);
  entry_context_menu(context, item);
  entry_drag_source(item, row.depth);
  handle_row_keys(context, item);

  // A click previews the patch, a double click keeps it open.
  if (clicked && context.open_patch) {
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

/// A closed folder that takes the entry held over it opens after a moment,
/// as ImGui's hold-to-open does, so the drag can go on into it.
void spring_open(PatchTreeCache &cache, const patches::TreeRow *held) {
  constexpr double kHoldToOpen = 0.7;
  static std::string folder;
  static double since = 0.0;
  if (held == nullptr || held->is_open) {
    folder.clear();
    return;
  }
  const double now = ImGui::GetTime();
  if (folder != held->entry->relative_path) {
    folder = held->entry->relative_path;
    since = now;
  } else if (now - since >= kHoldToOpen) {
    cache.set_open(folder, true);
    folder.clear();
  }
}

/**
 * Makes row `index`, across the whole width, take an entry for its folder.
 * True with `hover` filled while one that folder can take is held over it.
 */
/// Row `index` taking files dragged in from outside for its folder, which
/// they are copied into.
bool files_drop_target(PatchSelectorContext &context,
                       const std::vector<patches::TreeRow> &rows,
                       std::size_t index, std::size_t folder_index,
                       const ImRect &rect, DropHover &hover, DropAction &held) {
  const auto &folder = *rows[folder_index].entry;
  if (!context.copy_files_in || !context.repository.takes_copies(folder)) {
    return false;
  }
  if (!ImGui::BeginDragDropTargetCustom(
          rect, ImGui::GetID(rows[index].entry->relative_path.c_str()))) {
    return false;
  }
  bool holding = false;
  if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(
          os_file_drop::kPayload,
          ImGuiDragDropFlags_AcceptBeforeDelivery |
              ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
    if (payload->IsPreview()) {
      hover = {folder_index};
      held = DropAction::Copy;
      holding = true;
    }
    if (payload->IsDelivery()) {
      context.pending_files = PendingFiles{os_file_drop::take(), folder};
    }
  }
  ImGui::EndDragDropTarget();
  return holding;
}

bool row_drop_target(PatchSelectorContext &context,
                     const std::vector<patches::TreeRow> &rows,
                     std::size_t index, const ImRect &rect, DropHover &hover,
                     DropAction &held) {
  const std::size_t folder_index = patches::drop_folder_index(rows, index);
  if (folder_index == std::string::npos) {
    return false;
  }
  const ImGuiPayload *pending = ImGui::GetDragDropPayload();
  if (pending == nullptr) {
    return false;
  }
  if (pending->IsDataType(os_file_drop::kPayload)) {
    return files_drop_target(context, rows, index, folder_index, rect, hover,
                             held);
  }
  if (!pending->IsDataType(kPatchEntryPayload)) {
    return false;
  }
  const std::string relative_path(static_cast<const char *>(pending->Data),
                                  static_cast<std::size_t>(pending->DataSize));
  const auto *source =
      find_by_relative_path(context.repository.tree(), relative_path);
  if (source == nullptr) {
    return false;
  }
  const auto &folder = *rows[folder_index].entry;
  const DropAction action = drop_action(context, *source, folder);
  if (action == DropAction::None) {
    return false;
  }
  if (!ImGui::BeginDragDropTargetCustom(
          rect, ImGui::GetID(rows[index].entry->relative_path.c_str()))) {
    return false;
  }
  bool holding = false;
  if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(
          kPatchEntryPayload, ImGuiDragDropFlags_AcceptBeforeDelivery |
                                  ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
    if (payload->IsPreview()) {
      hover = {folder_index};
      held = action;
      holding = true;
    }
    if (payload->IsDelivery()) {
      context.pending_move =
          PendingMove{*source, folder, action == DropAction::Copy};
    }
  }
  ImGui::EndDragDropTarget();
  return holding;
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
void row_tops(const std::vector<patches::TreeRow> &rows,
              std::vector<float> &tops) {
  const float gap = ImGui::GetStyle().ItemSpacing.y;
  const float folder_height = row_height();
  const float patch_height = ui::patch_row_height();
  tops.clear();
  tops.reserve(rows.size() + 1);
  float y = 0.0f;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (i > 0 && is_heading(rows[i])) {
      y += gap;
    }
    tops.push_back(y);
    y += rows[i].is_directory ? folder_height : patch_height;
  }
  tops.push_back(y);
}

void open_tree_directory(const std::string &relative_path) {
  tree_cache().set_open(relative_path, true);
}

void follow_tree_directory(const std::string &from, const std::string &to) {
  tree_cache().follow(from, to);
}

bool render_patch_tree(const std::vector<patches::PatchEntry> &tree,
                       PatchSelectorContext &context,
                       const std::string &query_lower,
                       patches::StarFilter shown_stars) {
  PatchTreeCache &cache = tree_cache();
  const auto &selection = context.session.current_patch_selection_path();
  const bool reveal =
      cache.reveal(tree, selection, context.repository.revision(),
                   !query_lower.empty() || shown_stars.active());
  const auto &rows =
      cache.get(tree, context.repository.revision(), query_lower, shown_stars);
  if (rows.empty()) {
    return false;
  }

  const float line = ImGui::GetTextLineHeight();
  const float gap = ImGui::GetStyle().ItemSpacing.y;
  if (cache.tops_build != cache.rows_build || cache.tops_line != line ||
      cache.tops_gap != gap) {
    row_tops(rows, cache.tops);
    cache.tops_build = cache.rows_build;
    cache.tops_line = line;
    cache.tops_gap = gap;
  }
  const std::vector<float> &tops = cache.tops;
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
  const float height = ui::patch_row_height();
  const bool can_drop = context.move_patch || context.copy_patch;
  std::optional<DropHover> hover;
  DropAction held_drop = DropAction::None;
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
      render_directory_row(context, row, cache);
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
                        held, held_drop)) {
      hover = held;
    }
  }
  g_held_drop = held_drop;
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
  spring_open(cache, hover ? &rows[hover->folder] : nullptr);
  draw_drag_tooltip(context);

  return true;
}

} // namespace ui::selector_detail
