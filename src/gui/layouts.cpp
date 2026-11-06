#include "gui/layouts.hpp"

#include "gui/areas.hpp"
#include "gui/ui_scale.hpp"
#include "gui/window_title.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <string>

namespace ui::layouts {

namespace {

constexpr float kEditorMinWidth = 300.0f;
constexpr float kBrowserMinWidth = 230.0f;

/// Too narrow for the patch browser beside the editor: the browser folds
/// away and the waveform goes above the keyboard.
bool window_is_narrow() {
  return ImGui::GetMainViewport()->WorkSize.x <
         ui::scale::px(kEditorMinWidth + kBrowserMinWidth);
}

/// The most of the window's height the footer takes when it grows.
constexpr float kFooterMaxShare = 0.6f;
/// The waveform's share of the footer side by side, as the default lays out.
constexpr float kWaveformShareBeside = 0.20f;
constexpr float kWaveformShareAbove = 0.40f;

/// The waveform's share of the footer in each direction, kept from the last
/// time it was split that way so dragging it sticks.
float waveform_share[2] = {kWaveformShareBeside, kWaveformShareAbove};

bool browser_folded = false;

/// The widths the browser and the editor were given last, by a drag of the
/// divider or a layout, and the browser width this code set since.
struct SidebarWidths {
  float browser = 0.0f;
  float editor = 0.0f;
  float set = -1.0f;
} sidebar;

} // namespace

void build_default_layout(unsigned int dockspace_id) {
  ImGuiID dock_main_id = dockspace_id;

  ImGuiID doc_id_down = ImGui::DockBuilderSplitNode(
      dock_main_id, ImGuiDir_Down, 0.23f, nullptr, &dock_main_id);

  ImGuiID doc_id_down_left = ImGui::DockBuilderSplitNode(
      doc_id_down, ImGuiDir_Left, kWaveformShareBeside, nullptr, &doc_id_down);
  waveform_share[ImGuiAxis_X] = kWaveformShareBeside;
  waveform_share[ImGuiAxis_Y] = kWaveformShareAbove;

  ImGuiID dock_id_left = ImGui::DockBuilderSplitNode(
      dock_main_id, ImGuiDir_Left, 0.26f, nullptr, &dock_main_id);

  // This ImGui version clears DockOrder in DockBuilderDockWindow; render_all
  // submits these windows in the same editor-then-Preferences tab order.
  const auto patch_editor_title = std::string(ui::PATCH_EDITOR_TITLE) + "###" +
                                  std::string(ui::PATCH_EDITOR_TITLE);
  ImGui::DockBuilderDockWindow(patch_editor_title.c_str(), dock_main_id);
  // The other documents' editor windows join the first as tabs, those only
  // remembered from before too, so none comes back where it used to be.
  const auto is_other_editor = [](std::string_view name) {
    if (const auto at = name.find("###"); at != std::string_view::npos) {
      name.remove_prefix(at + 3);
    }
    return ui::areas::is_editor_window(name) &&
           name != ui::PATCH_EDITOR_TITLE;
  };
  for (const ImGuiWindow *window : GImGui->Windows) {
    if (is_other_editor(window->Name)) {
      ImGui::DockBuilderDockWindow(window->Name, dock_main_id);
    }
  }
  for (ImGuiWindowSettings *settings = GImGui->SettingsWindows.begin();
       settings != nullptr;
       settings = GImGui->SettingsWindows.next_chunk(settings)) {
    if (is_other_editor(settings->GetName())) {
      settings->DockId = dock_main_id;
    }
  }
  ImGui::DockBuilderDockWindow(ui::PREFERENCES_TITLE, dock_main_id);
  ImGui::DockBuilderDockWindow(ui::PATCH_BROWSER_TITLE, dock_id_left);

  ImGui::DockBuilderDockWindow(ui::SOFT_KEYBOARD_TITLE, doc_id_down);
  ImGui::DockBuilderDockWindow(ui::PATCH_LAB_TITLE, doc_id_down);

  ImGui::DockBuilderDockWindow(ui::WAVEFORM_TITLE, doc_id_down_left);
}

void fit_footer_to_width() {
  const ImGuiWindow *waveform = ImGui::FindWindowByName(ui::WAVEFORM_TITLE);
  if (waveform == nullptr || !waveform->WasActive ||
      waveform->DockNode == nullptr) {
    return;
  }
  ImGuiDockNode *footer = waveform->DockNode->ParentNode;
  if (footer == nullptr || !footer->IsSplitNode() ||
      footer->ChildNodes[0] != waveform->DockNode) {
    return;
  }

  const ImGuiAxis was = footer->SplitAxis;
  const ImGuiAxis axis = window_is_narrow() ? ImGuiAxis_Y : ImGuiAxis_X;
  if (axis == was) {
    return;
  }

  if (footer->Size[was] > 0.0f) {
    waveform_share[was] = waveform->DockNode->Size[was] / footer->Size[was];
  }
  // The keyboard's area keeps its height: the footer grows by the
  // waveform's when stacking it on top, and gives that back after.
  ImGuiDockNode *frame = footer->ParentNode;
  ImVec2 size = footer->Size;
  if (frame != nullptr && frame->IsSplitNode() &&
      frame->SplitAxis == ImGuiAxis_Y && frame->ChildNodes[1] == footer) {
    const float keyboard_share = 1.0f - waveform_share[ImGuiAxis_Y];
    size.y = ImMin(axis == ImGuiAxis_Y ? size.y / keyboard_share
                                       : size.y * keyboard_share,
                   frame->Size.y * kFooterMaxShare);
    footer->SizeRef.y = size.y;
    frame->ChildNodes[0]->SizeRef.y = frame->Size.y - size.y;
  }

  ImGuiDockNode *other = footer->ChildNodes[1];
  footer->SplitAxis = axis;
  waveform->DockNode->SizeRef[axis] = size[axis] * waveform_share[axis];
  other->SizeRef[axis] = size[axis] - waveform->DockNode->SizeRef[axis];
  waveform->DockNode->SizeRef[was] = other->SizeRef[was] = size[was];
  ImGui::MarkIniSettingsDirty();
}

void fit_sidebar_to_width() {
  browser_folded = window_is_narrow();
  if (browser_folded) {
    return;
  }
  const ImGuiWindow *browser = ImGui::FindWindowByName(ui::PATCH_BROWSER_TITLE);
  if (browser == nullptr || !browser->WasActive ||
      browser->DockNode == nullptr) {
    return;
  }
  ImGuiDockNode *node = browser->DockNode;
  ImGuiDockNode *top = node->ParentNode;
  if (top == nullptr || !top->IsSplitNode() || top->SplitAxis != ImGuiAxis_X ||
      top->ChildNodes[0] != node || top->Size.x <= 0.0f) {
    return;
  }

  // A width this code did not set came from the divider or a layout, and
  // is what the two go back to as the window widens. Neither is taken
  // below its least width.
  const float browser_min = ui::scale::px(kBrowserMinWidth);
  if (node->SizeRef.x != sidebar.set) {
    sidebar.browser = ImClamp(
        node->SizeRef.x, browser_min,
        ImMax(browser_min, top->Size.x - ui::scale::px(kEditorMinWidth)));
    sidebar.editor = top->Size.x - sidebar.browser;
  }
  const float width = ImMin(
      sidebar.browser, ImMax(top->Size.x - sidebar.editor, browser_min));
  node->SizeRef.x = width;
  sidebar.set = width;
}

bool patch_browser_folded() { return browser_folded; }

float patch_browser_min_width() { return ui::scale::px(kBrowserMinWidth); }

} // namespace ui::layouts
