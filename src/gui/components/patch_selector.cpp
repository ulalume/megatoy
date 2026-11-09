#include "patch_selector.hpp"
#include "gui/app_window.hpp"

#include "gui/areas.hpp"
#include "gui/components/folded_tab_button.hpp"
#include "gui/layouts.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include "gui/window_title.hpp"
#include "patch_selector_shared.hpp"
#include "patch_tree_view.hpp"

#include "gui/fonts/megatoy_icons.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <utility>

namespace ui {

namespace {

using namespace selector_detail;

void render_empty_workspace_prompt(PatchSelectorContext &context) {
  if (PreferenceManager::folder_add_is_import()) {
    ImGui::TextWrapped("Import a local folder of patches -- "
                       "megatoy copies it into browser storage.");
  } else {
    ImGui::TextWrapped("No patch folders yet.");
  }
}

const char *add_folder_label() {
  return PreferenceManager::folder_add_is_import() ? "Import Folder..."
                                                   : "Add Folder...";
}

/// The labelled button, under the prompt shown while there are no folders.
void render_add_folder_button(PatchSelectorContext &context) {
  const std::string label =
      std::string(ICON_MEGATOY_FOLDER_PLUS " ") + add_folder_label();
  if (ImGui::Button(label.c_str())) {
    context.add_folder();
  }
  ImGui::Separator();
}

/// The same action as an icon at the right end of the filter row. Drawn
/// before the row, which is kept clear of it; returns the width it takes.
float render_add_folder_icon(PatchSelectorContext &context) {
  const ImVec2 row = ImGui::GetCursorScreenPos();
  const float size = ImGui::GetFrameHeight();
  const float right = ImGui::GetCurrentWindow()->WorkRect.Max.x;
  ImGui::SetCursorScreenPos(ImVec2(right - size, row.y));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  if (ImGui::Button(ICON_MEGATOY_FOLDER_PLUS "##add_folder",
                    ImVec2(size, size))) {
    context.add_folder();
  }
  ImGui::PopStyleColor();
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", add_folder_label());
  }
  ImGui::SetCursorScreenPos(row);
  return size + ImGui::GetStyle().ItemSpacing.x;
}

/// The search row's band reaches this far below it, and the tree starts
/// this far below the band, before the interface's scale.
constexpr float kHeaderPadding = 2.0f;
constexpr float kHeaderGap = 2.0f;

void render_tree(PatchSelectorContext &context) {
  // Scrolls on its own, so the filter row above stays in place. It reaches
  // the window's edges; its own padding keeps the rows where they were.
  ImGuiWindow *window = ImGui::GetCurrentWindow();
  const ImRect inner = window->InnerRect;
  const float top = ImGui::GetCursorScreenPos().y;
  ImGui::SetCursorScreenPos(ImVec2(inner.Min.x, top));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(ImGui::GetStyle().WindowPadding.x, 0.0f));
  const bool visible = ImGui::BeginChild(
      "PresetTree", ImVec2(inner.GetWidth(), inner.Max.y - top),
      ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PopStyleVar();
  if (visible) {
    // Folder and file rows alike run to a pixel short of the area's edges:
    // a file row's highlight would otherwise stop half a spacing past a
    // folder row's. Their contents stay in by the padding.
    ImGuiWindow *tree = ImGui::GetCurrentWindow();
    const float right = tree->InnerRect.Max.x - ui::scale::px(1.0f);
    tree->WorkRect.Min.x = tree->InnerRect.Min.x + ui::scale::px(1.0f);
    tree->WorkRect.Max.x = right;
    ImGui::PushClipRect(tree->InnerRect.Min,
                        ImVec2(right, tree->InnerRect.Max.y), false);
    const auto search =
        patches::parse_search(context.prefs.metadata_search_query);
    const bool rendered = render_patch_tree(context.repository.tree(), context,
                                            search.text_lower, search.stars);
    ImGui::PopClipRect();
    if (!rendered && (!search.text_lower.empty() || search.stars.active())) {
      ImGui::TextColored(styles::color(styles::MegatoyCol::TextMuted),
                         "No results for current filters");
    }
  }
  ImGui::EndChild();
}

/// Everything inside the patch browser, whether a window or folded away.
void render_patch_selector_contents(PatchSelectorContext &context) {
  if (context.workspace_is_empty && context.add_folder) {
    render_empty_workspace_prompt(context);
    render_add_folder_button(context);
  }

  // The add-folder icon closes the filter row, which is kept clear of it.
  // Docked, the row stands at the window's top, as tall as the editors' tabs
  // beside it, its contents as far in as the folders' below.
  ImGuiWindow *window = ImGui::GetCurrentWindow();
  if (window->DockNode != nullptr) {
    ImGui::SetCursorPosY(0.0f);
  }
  ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding,
                       (tab_height() - ImGui::GetFontSize()) * 0.5f);
  // The row stands on a band of its own, across the whole window and from
  // a pixel above it, under the menu bar's edge, to kHeaderPadding below the
  // row; the tree starts kHeaderGap below that.
  const float header_top = ImGui::GetCursorScreenPos().y;
  const float header_bottom =
      header_top + ImGui::GetFrameHeight() + ui::scale::px(kHeaderPadding);
  window->DrawList->PushClipRect(
      ImVec2(window->Pos.x, window->Pos.y - ui::scale::px(1.0f)),
      ImVec2(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y));
  window->DrawList->AddRectFilled(
      ImVec2(window->Pos.x, header_top - ui::scale::px(1.0f)),
      ImVec2(window->Pos.x + window->Size.x, header_bottom),
      styles::color_u32(styles::MegatoyCol::BrowserHeaderBg));
  window->DrawList->PopClipRect();
  const float work_right = window->WorkRect.Max.x;
  if (context.add_folder) {
    window->WorkRect.Max.x -= render_add_folder_icon(context);
  }
  render_filter_bar(context);
  window->WorkRect.Max.x = work_right;
  ImGui::PopStyleVar();
  ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x,
                                   header_bottom + ui::scale::px(kHeaderGap)));
  render_tree(context);

  if (context.pending_remove_folder && context.remove_folder) {
    const auto folder = *context.pending_remove_folder;
    context.pending_remove_folder.reset();
    context.remove_folder(folder);
  }

  if (context.pending_move && context.move_patch) {
    const auto move = std::move(*context.pending_move);
    context.pending_move.reset();
    if (move.entry.is_directory) {
      follow_tree_directory(move.entry.relative_path,
                            move.destination.relative_path + "/" +
                                move.entry.full_path.filename().string());
    }
    context.move_patch(move.entry, move.destination);
  }

  if (context.pending_rating) {
    const auto rating = std::move(*context.pending_rating);
    context.pending_rating.reset();
    if (const auto *entry = context.session.find_entry(rating.relative_path)) {
      context.session.set_entry_rating(*entry, rating.stars);
    }
  }

  render_icon_picker(context);
  if (context.pending_icon) {
    const auto icon = std::move(*context.pending_icon);
    context.pending_icon.reset();
    if (const auto *entry = context.session.find_entry(icon.relative_path)) {
      context.session.set_entry_icon(*entry, icon.icon);
    }
  }

  if (context.pending_menu_action) {
    context.pending_menu_action.reset();
    if (context.save_current_patch) {
      context.save_current_patch();
    }
  }
}

/**
 * The patch browser folded into a button at the left of the editors' tabs.
 * It opens the browser as a menu does, over the editor at its least width,
 * and leaves it open while patches are chosen so each shows behind it; a
 * click outside closes it.
 */
void render_folded_patch_selector(PatchSelectorContext &context) {
  static const ImGuiID popup_id = ImHashStr("##folded_patch_browser");
  // Whether the browser was open when the button was pressed: that press
  // closed it, as a press outside it does, and the release leaves it so.
  static bool open_at_press = false;

  const auto button = folded_tab_button(
      ImGui::DockBuilderGetCentralNode(ImGui::GetMainViewport()->ID),
      "##folded_patch_browser", PATCH_BROWSER_ICON, PATCH_BROWSER_TITLE);
  if (!button) {
    return;
  }
  const ImVec2 below(button->min.x, button->max.y);
  // Also through the open browser, so a press here while it is open is
  // known for the one that closes it.
  if (button->under_pointer) {
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      open_at_press = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);
    }
    // On release, so the press is not taken for one outside the browser.
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !open_at_press) {
      ImGui::OpenPopupEx(popup_id);
    }
  }

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const float bottom = viewport->WorkPos.y + viewport->WorkSize.y;
  ImGui::SetNextWindowPos(below);
  ImGui::SetNextWindowSize(ImVec2(layouts::patch_browser_min_width(),
                                  ImMax(0.0f, bottom - below.y)));
  if (!ImGui::BeginPopupEx(popup_id,
                           ImGuiWindowFlags_NoTitleBar |
                               ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoResize |
                               ImGuiWindowFlags_NoSavedSettings |
                               ImGuiWindowFlags_NoScrollbar |
                               ImGuiWindowFlags_NoScrollWithMouse)) {
    return;
  }
  ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
  render_patch_selector_contents(context);
  ImGui::PopItemFlag();
  ImGui::EndPopup();
}

/// The closed patch browser as the same button, a click on which brings
/// the sidebar back.
void render_closed_patch_selector(bool &show) {
  static bool pressed = false;
  const auto button = folded_tab_button(
      ImGui::DockBuilderGetCentralNode(ImGui::GetMainViewport()->ID),
      "##folded_patch_browser", PATCH_BROWSER_ICON, PATCH_BROWSER_TITLE);
  if (folded_tab_button_clicked(button, pressed)) {
    show = true;
  }
}

} // namespace

void render_patch_selector(const char *title, PatchSelectorContext &context) {
  auto &prefs = context.prefs;
  // Folded on a narrow screen, opened over the editor from its button;
  // closed on a wide one, brought back as the sidebar from it.
  if (layouts::window_is_narrow()) {
    render_folded_patch_selector(context);
    return;
  }
  if (!prefs.show_patch_selector) {
    render_closed_patch_selector(prefs.show_patch_selector);
    return;
  }

  ImGui::SetNextWindowPos(ui::scale::px(ImVec2(50, 400)),
                          ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ui::scale::px(ImVec2(350, 500)),
                           ImGuiCond_FirstUseEver);

  areas::keep_in_area(areas::Area::Sidebar, title, ImGuiDockNodeFlags_NoTabBar);
  // Only the tree scrolls; it runs to the bottom edge.
  if (!ui::begin_window(title, &prefs.show_patch_selector,
                        ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoScrollWithMouse)) {
    ImGui::End();
    return;
  }
  render_patch_selector_contents(context);
  ImGui::End();
}

} // namespace ui
