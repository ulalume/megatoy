#include "gui/areas.hpp"

#include "gui/window_title.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <map>
#include <set>
#include <string>

namespace ui::areas {

namespace {

ImGuiDockNode *find_node(ImGuiID id) {
  return id != 0 ? ImGui::DockContextFindNodeByID(GImGui, id) : nullptr;
}

ImGuiDockNode *central_node() {
  return ImGui::DockBuilderGetCentralNode(ImGui::GetMainViewport()->ID);
}

/// Where each view was last docked, by window id: from the saved settings,
/// replaced by the window's own state once it exists.
std::map<ImGuiID, std::pair<Area, ImGuiID>> view_docks() {
  ImGuiContext &g = *GImGui;
  std::map<ImGuiID, std::pair<Area, ImGuiID>> docks;
  for (ImGuiWindowSettings *settings = g.SettingsWindows.begin();
       settings != nullptr; settings = g.SettingsWindows.next_chunk(settings)) {
    if (settings->WantDelete) {
      continue;
    }
    if (const auto area = area_of(settings->GetName())) {
      docks[settings->ID] = {*area, settings->DockId};
    }
  }
  for (const ImGuiWindow *window : g.Windows) {
    if (const auto area = area_of(window->Name)) {
      docks[window->ID] = {*area, window->DockId};
    }
  }
  return docks;
}

/**
 * The part of the dock tree the main area spans: the central group and the
 * groups it was split into, up to where a side area's group joins in.
 */
const ImGuiDockNode *main_region() {
  const ImGuiDockNode *region = central_node();
  if (region == nullptr) {
    return nullptr;
  }
  std::set<const ImGuiDockNode *> holding_side;
  for (const auto &[id, dock] : view_docks()) {
    if (dock.first == Area::Main) {
      continue;
    }
    for (const ImGuiDockNode *node = find_node(dock.second); node != nullptr;
         node = node->ParentNode) {
      holding_side.insert(node);
    }
  }
  while (region->ParentNode != nullptr &&
         !holding_side.contains(region->ParentNode)) {
    region = region->ParentNode;
  }
  return region;
}

bool is_within(const ImGuiDockNode *node, const ImGuiDockNode *region) {
  for (; node != nullptr; node = node->ParentNode) {
    if (node == region) {
      return true;
    }
  }
  return false;
}

} // namespace

bool is_editor_window(std::string_view window_name) {
  // Saved settings keep only what follows "###".
  if (const auto at = window_name.find("###"); at != std::string_view::npos) {
    window_name.remove_prefix(at + 3);
  }
  const std::string_view title(PATCH_EDITOR_TITLE);
  if (!window_name.starts_with(title)) {
    return false;
  }
  window_name.remove_prefix(title.size());
  return window_name.empty() ||
         (window_name.size() > 1 && window_name.front() == ' ' &&
          window_name.substr(1).find_first_not_of("0123456789") ==
              std::string_view::npos);
}

std::optional<Area> area_of(std::string_view window_name) {
  if (window_name == PATCH_BROWSER_TITLE) {
    return Area::Sidebar;
  }
  if (window_name == PREFERENCES_TITLE || is_editor_window(window_name)) {
    return Area::Main;
  }
  if (window_name == WAVEFORM_TITLE) {
    return Area::FooterLeft;
  }
  if (window_name == SOFT_KEYBOARD_TITLE || window_name == MML_CONSOLE_TITLE ||
      window_name == PATCH_LAB_TITLE) {
    return Area::FooterRight;
  }
  return std::nullopt;
}

void keep_in_area(Area area, const char *window_name, unsigned int node_flags,
                  unsigned int main_dock_id) {
  // The groups have no close-all button: each tab closes on its own, and
  // hiding a view is the View menu's.
  ImGuiWindowClass window_class;
  window_class.DockNodeFlagsOverrideSet =
      static_cast<ImGuiDockNodeFlags>(node_flags) |
      ImGuiDockNodeFlags_NoCloseButton;
  if (area != Area::Main) {
    window_class.DockNodeFlagsOverrideSet |=
        ImGuiDockNodeFlags_NoUndocking | ImGuiDockNodeFlags_NoDockingSplit |
        ImGuiDockNodeFlags_NoDockingOverMe;
  }
  ImGui::SetNextWindowClass(&window_class);
  if (area != Area::Main) {
    return;
  }

  // A tab being dragged is left alone until it is let go.
  const ImGuiWindow *window = ImGui::FindWindowByName(window_name);
  if (window == nullptr || !window->WasActive ||
      GImGui->MovingWindow == window) {
    return;
  }
  const ImGuiDockNode *region = main_region();
  if (region == nullptr) {
    return;
  }
  const bool floating = window->DockNode == nullptr && window->DockId == 0;
  const bool outside =
      window->DockNode != nullptr && !is_within(window->DockNode, region);
  if (!floating && !outside) {
    return;
  }
  const ImGuiDockNode *target = find_node(main_dock_id);
  if (target == nullptr || !is_within(target, region)) {
    target = central_node();
  }
  ImGui::SetNextWindowDockID(target->ID, ImGuiCond_Always);
}

void refuse_outer_docking(unsigned int dockspace_id) {
  // Set on the root alone: the groups below keep their own flags, so the main
  // area can still be split.
  ImGuiDockNode *root = ImGui::DockBuilderGetNode(dockspace_id);
  if (root != nullptr && root->IsSplitNode() &&
      (root->LocalFlags & ImGuiDockNodeFlags_NoDockingSplit) == 0) {
    root->SetLocalFlags(root->LocalFlags | ImGuiDockNodeFlags_NoDockingSplit);
  }
}

bool layout_fits_areas() {
  const ImGuiID dockspace_id = ImGui::GetMainViewport()->ID;
  std::map<ImGuiID, std::set<Area>> areas_in_node;
  std::map<Area, std::set<ImGuiID>> side_nodes;
  for (const auto &[id, dock] : view_docks()) {
    const auto [area, dock_id] = dock;
    const ImGuiDockNode *node = find_node(dock_id);
    if (area != Area::Main) {
      // A side view outside the window's dock tree, or apart from the rest
      // of its area, is out of place.
      if (node == nullptr ||
          ImGui::DockNodeGetRootNode(const_cast<ImGuiDockNode *>(node))->ID !=
              dockspace_id) {
        return false;
      }
      side_nodes[area].insert(dock_id);
    }
    if (node != nullptr) {
      areas_in_node[dock_id].insert(area);
    }
  }
  for (const auto &[node, areas] : areas_in_node) {
    if (areas.size() > 1) {
      return false;
    }
  }
  for (const auto &[area, nodes] : side_nodes) {
    if (nodes.size() > 1) {
      return false;
    }
  }
  return true;
}

} // namespace ui::areas
