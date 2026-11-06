#include "gui/layouts.hpp"

#include "gui/areas.hpp"
#include "gui/ui_scale.hpp"
#include "gui/window_title.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <string>

namespace ui::layouts {

namespace {

// Apart, so a width between the two does not flip the footer back and forth.
constexpr float kStackBelowWidth = 600.0f;
constexpr float kSideBySideFromWidth = 640.0f;
/// The most of the window's height the footer takes when it grows.
constexpr float kFooterMaxShare = 0.6f;

} // namespace

void build_default_layout(unsigned int dockspace_id) {
  ImGuiID dock_main_id = dockspace_id;

  ImGuiID doc_id_down = ImGui::DockBuilderSplitNode(
      dock_main_id, ImGuiDir_Down, 0.23f, nullptr, &dock_main_id);

  ImGuiID doc_id_down_left = ImGui::DockBuilderSplitNode(
      doc_id_down, ImGuiDir_Left, 0.20f, nullptr, &doc_id_down);

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

  const float width = ImGui::GetMainViewport()->WorkSize.x;
  const ImGuiAxis was = footer->SplitAxis;
  ImGuiAxis axis = was;
  if (width < ui::scale::px(kStackBelowWidth)) {
    axis = ImGuiAxis_Y;
  } else if (width >= ui::scale::px(kSideBySideFromWidth)) {
    axis = ImGuiAxis_X;
  }
  if (axis == was) {
    return;
  }

  // The waveform's share of each direction, kept from the last time the
  // footer was split that way: as built at first, then as dragged.
  static float waveform_share[2] = {0.20f, 0.40f};
  if (footer->Size[was] > 0.0f) {
    waveform_share[was] = waveform->DockNode->Size[was] / footer->Size[was];
  }
  // The keyboard's area keeps its height: the footer grows by the
  // waveform's when stacking it on top, and gives that back after.
  ImGuiDockNode *frame = footer->ParentNode;
  ImVec2 size = footer->Size;
  if (frame != nullptr && frame->IsSplitNode() &&
      frame->SplitAxis == ImGuiAxis_Y && frame->ChildNodes[1] == footer) {
    const float keyboard_height =
        axis == ImGuiAxis_Y ? footer->Size.y
                            : footer->Size.y * (1.0f - waveform_share[was]);
    size.y = axis == ImGuiAxis_Y
                 ? keyboard_height / (1.0f - waveform_share[axis])
                 : keyboard_height;
    size.y = ImMin(size.y, frame->Size.y * kFooterMaxShare);
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

} // namespace ui::layouts
