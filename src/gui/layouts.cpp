#include "gui/layouts.hpp"

#include "gui/areas.hpp"
#include "gui/window_title.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <string>

namespace ui::layouts {

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
  ImGui::DockBuilderDockWindow(ui::MML_CONSOLE_TITLE, doc_id_down);
  ImGui::DockBuilderDockWindow(ui::PATCH_LAB_TITLE, doc_id_down);

  ImGui::DockBuilderDockWindow(ui::WAVEFORM_TITLE, doc_id_down_left);
}

} // namespace ui::layouts
