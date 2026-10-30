#include "gui/layouts.hpp"

#include "gui/areas.hpp"
#include "gui/window_title.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>

namespace ui::layouts {

namespace {

void build_sound_layout(unsigned int dockspace_id) {
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

constexpr std::array kLayouts{
    LayoutDefinition{LayoutId::Sound, "Sound", WindowSet{}, build_sound_layout},
};

std::string storage_suffix(std::string_view layout_name) {
  std::string suffix(layout_name);
  std::transform(suffix.begin(), suffix.end(), suffix.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return suffix;
}

bool is_default_layout_name(std::string_view layout_name) {
  return layout_name == definition(kDefaultLayout).name;
}

} // namespace

std::span<const LayoutDefinition> builtin_layouts() { return kLayouts; }

const LayoutDefinition &definition(LayoutId id) {
  for (const auto &layout : kLayouts) {
    if (layout.id == id) {
      return layout;
    }
  }
  return kLayouts.front();
}

std::optional<LayoutId> find_layout(std::string_view name) {
  for (const auto &layout : kLayouts) {
    if (layout.name == name) {
      return layout.id;
    }
  }
  return std::nullopt;
}

LayoutId resolve_layout(std::string_view name) {
  return find_layout(name).value_or(kDefaultLayout);
}

void apply_windows(const WindowSet &windows, UIPreferences &prefs) {
  prefs.show_patch_selector = windows.patch_browser;
  prefs.show_patch_editor = windows.patch_editor;
  prefs.show_patch_lab = windows.patch_lab;
  prefs.show_midi_keyboard = windows.soft_keyboard;
  prefs.show_mml_console = windows.mml_console;
  prefs.show_waveform = windows.waveform;
  prefs.show_preferences = windows.preferences;
}

std::filesystem::path ini_file_path(const std::filesystem::path &base_file,
                                    std::string_view layout_name) {
  if (base_file.empty() || is_default_layout_name(layout_name)) {
    return base_file;
  }
  auto file_name = base_file.stem().string() + "-" +
                   storage_suffix(layout_name) +
                   base_file.extension().string();
  return base_file.parent_path() / file_name;
}

std::string ini_storage_key(std::string_view base_key,
                            std::string_view layout_name) {
  std::string key(base_key);
  if (!is_default_layout_name(layout_name)) {
    key += "_" + storage_suffix(layout_name);
  }
  return key;
}

} // namespace ui::layouts
