#include "patch_selector_shared.hpp"
#include "gui/components/instrument_icon_picker.hpp"
#include "gui/components/menu_rows.hpp"

#include "common.hpp"
#include "file_manager.hpp"
#include "gui/save_export_actions.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include "patch_tree_view.hpp"
#include "patches/instrument_icons.hpp"
#include "patches/patch_entry_actions.hpp"
#include "patches/patch_filter.hpp"
#include "platform/platform_config.hpp"
#include "star_rating.hpp"

#include "gui/fonts/megatoy_icons.hpp"
#include <algorithm>
#include <cfloat>
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
    tooltip +=
        "\nRating: " + std::to_string(entry.metadata->star_rating) + "/5";
    if (!entry.metadata->icon.empty()) {
      tooltip += "\nIcon: " +
                 patches::instrument_icons::display_name(entry.metadata->icon);
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
  if (!ui::begin_popup_context_item()) {
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
    if (actions.create_patch && ImGui::MenuItem("New Patch...")) {
      context.create_patch_in(entry.full_path);
    }
    if (actions.create_folder && ImGui::MenuItem("New Folder...")) {
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
        if (ImGui::MenuItem("Save to Browser Storage...")) {
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
  if (begin_group(actions.rate)) {
    if (ui::begin_menu("Icon")) {
      const std::string icon =
          entry.metadata ? entry.metadata->icon : std::string();
      if (auto picked = instrument_icon_list(icon)) {
        context.pending_icon =
            PendingIcon{entry.relative_path, std::move(*picked)};
        ImGui::CloseCurrentPopup();
      }
      ui::end_menu();
    }
    if (ui::begin_menu("Rating")) {
      const int rating = entry.metadata ? entry.metadata->star_rating : 0;
      for (int stars = 0; stars <= 5; ++stars) {
        const char *label = stars == 0 ? "None" : kStarLabels[stars].data();
        if (ImGui::MenuItem(label, nullptr, stars == rating)) {
          context.pending_rating = PendingRating{entry.relative_path, stars};
        }
      }
      ui::end_menu();
    }
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

  ui::end_popup();
}

namespace {

/**
 * Below the search box, while it is typed in or the pointer is on the menu:
 * a row for each rating, from one star to five. The rating picked, if any.
 */
std::optional<int> render_star_menu(const ImRect &box, bool typing,
                                    int current) {
  static bool open = false;
  open = open || typing;
  if (!open) {
    return std::nullopt;
  }
  const ImGuiStyle &style = ImGui::GetStyle();
  ImGui::SetNextWindowPos(ImVec2(box.Min.x, box.Max.y + style.ItemSpacing.y));
  ImGui::SetNextWindowSizeConstraints(ImVec2(box.GetWidth(), 0.0f),
                                      ImVec2(box.GetWidth(), FLT_MAX));
  ImGui::PushStyleColor(ImGuiCol_WindowBg,
                        ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, style.PopupRounding);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, style.PopupBorderSize);
  ImGui::Begin("##star_search", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoFocusOnAppearing |
                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking |
                   ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
  ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

  std::optional<int> picked;
  const ImU32 muted =
      ImGui::GetColorU32(styles::color(styles::MegatoyCol::TextMuted));
  ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, list_row_spacing());
  for (int stars = 1; stars <= 5; ++stars) {
    ImGui::PushID(stars);
    if (ImGui::Selectable("##stars", stars == current)) {
      picked = stars;
    }
    const ImRect row(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    draw_stars(row.Min, row.GetHeight(), stars);
    // What the box is given for the row, its digit level with the stars.
    const char word[3] = {'*', static_cast<char>('0' + stars), '\0'};
    const ImFontGlyph *digit = ImGui::GetFontBaked()->FindGlyph(word[1]);
    const float y =
        digit ? IM_ROUND((row.Min.y + row.Max.y - digit->Y0 - digit->Y1) * 0.5f)
              : row.Min.y;
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(row.Max.x - ImGui::CalcTextSize(word).x, y), muted, word);
    ImGui::PopID();
  }
  ImGui::PopStyleVar();

  const bool held =
      ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) ||
      ImGui::IsWindowFocused();
  ImGui::End();
  open = typing || held;
  return picked;
}

} // namespace

void render_filter_bar(PatchSelectorContext &context) {
  auto &prefs = context.prefs;
  char search_buffer[128];
  std::strncpy(search_buffer, prefs.metadata_search_query.c_str(),
               sizeof(search_buffer));
  search_buffer[sizeof(search_buffer) - 1] = '\0';

  // Back into the box once a rating is picked from its menu, typing on
  // after its text rather than over it.
  static bool refocus = false;
  static bool to_end = false;
  if (refocus) {
    ImGui::SetKeyboardFocusHere();
    refocus = false;
    to_end = true;
  }
  const auto place_cursor = [](ImGuiInputTextCallbackData *data) {
    if (to_end) {
      data->ClearSelection();
      data->CursorPos = data->BufTextLen;
      to_end = false;
    }
    return 0;
  };
  // The search box takes the row, up to where the caller has narrowed it to.
  ImGui::SetNextItemWidth(ImGui::GetCurrentWindow()->WorkRect.Max.x -
                          ImGui::GetCursorScreenPos().x);
  // The clear button sits over the box's right end and has to win the hover.
  ImGui::SetNextItemAllowOverlap();
  if (ImGui::InputTextWithHint(
          "##SharedSearch", ICON_MEGATOY_MAGNIFYING_GLASS " Search...",
          search_buffer, sizeof(search_buffer),
          ImGuiInputTextFlags_CallbackAlways, place_cursor)) {
    prefs.metadata_search_query = std::string(search_buffer);
  }
  const bool typing = ImGui::IsItemActive();
  const ImRect box(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  if (!prefs.metadata_search_query.empty()) {
    const float size = box.GetHeight();
    ImGui::SetCursorScreenPos(ImVec2(box.Max.x - size, box.Min.y));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (ImGui::Button(ICON_MEGATOY_XMARK "##clear_search",
                      ImVec2(size, size))) {
      prefs.metadata_search_query.clear();
    }
    ImGui::PopStyleColor(2);
  }

  if (const auto stars = render_star_menu(
          box, typing,
          patches::parse_search(prefs.metadata_search_query).min_stars)) {
    prefs.metadata_search_query =
        patches::with_star_word(prefs.metadata_search_query, *stars);
    refocus = true;
  }
}

} // namespace ui::selector_detail
