#include "../test_check.hpp"
#include "gui/areas.hpp"
#include "gui/window_title.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>

namespace {

/// A window laid out as the default layout lays it: the sidebar at the left,
/// the main area over the footer, and the footer split in two.
struct TestEnvironment {
  ImGuiID root;
  ImGuiID left;
  ImGuiID right;
  ImGuiID main = 0;
  ImGuiID footer_left = 0;
  ImGuiID footer_right = 0;

  TestEnvironment() {
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    root = ImGui::GetMainViewport()->ID;
    ImGui::DockBuilderAddNode(root);
    ImGui::DockBuilderSetNodeSize(root, ImVec2(1000, 800));
    ImGui::DockBuilderSplitNode(root, ImGuiDir_Left, 0.25f, &left, &right);
    ImGuiID footer = 0;
    ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.3f, &footer, &main);
    ImGui::DockBuilderSplitNode(footer, ImGuiDir_Left, 0.2f, &footer_left,
                                &footer_right);
  }

  void dock_side_views() const {
    ImGui::DockBuilderDockWindow(ui::LIBRARY_ID, left);
    ImGui::DockBuilderDockWindow(ui::WAVEFORM_TITLE, footer_left);
    ImGui::DockBuilderDockWindow(ui::SOFT_KEYBOARD_TITLE, footer_right);
    ImGui::DockBuilderDockWindow(ui::PATCH_LAB_TITLE, footer_right);
  }

  ~TestEnvironment() { ImGui::DestroyContext(); }
};

void test_editor_names_require_a_complete_stable_id() {
  for (const auto name : {"Patch Editor", "Patch Editor 2", "Patch Editor 123",
                          "Bass###Patch Editor", "Bass *###Patch Editor 2"}) {
    CHECK(ui::areas::is_editor_window(name));
    CHECK(ui::areas::area_of(name) == ui::areas::Area::Main);
  }
  for (const auto name :
       {"", "Patch Editor ", "Patch Editor -1", "Patch Editor 2x",
        "Patch Editor Help", "Patch Editor2", "patch editor", "Bass###Other",
        "Patch Editor##Tooltip"}) {
    CHECK(!ui::areas::is_editor_window(name));
    CHECK(!ui::areas::area_of(name));
  }
}

void test_saved_layout_accepts_main_splits_and_shared_side_tabs() {
  TestEnvironment env;
  env.dock_side_views();
  CHECK(ui::areas::layout_fits_areas());
  ImGuiID first_main = 0;
  ImGuiID second_main = 0;
  ImGui::DockBuilderSplitNode(env.main, ImGuiDir_Right, 0.5f, &first_main,
                              &second_main);
  ImGui::DockBuilderDockWindow("Warm###Patch Editor", first_main);
  ImGui::DockBuilderDockWindow("Cold###Patch Editor 2", second_main);
  ImGui::DockBuilderDockWindow(ui::PREFERENCES_TITLE, second_main);
  ImGui::DockBuilderDockWindow("Debug", second_main);
  CHECK(ui::areas::layout_fits_areas());
}

void test_saved_layout_rejects_mixed_areas_and_separate_side_tabs() {
  TestEnvironment env;
  env.dock_side_views();
  ImGui::DockBuilderDockWindow(ui::PREFERENCES_TITLE, env.left);
  CHECK(!ui::areas::layout_fits_areas());
  ImGui::DockBuilderDockWindow(ui::PREFERENCES_TITLE, env.main);
  CHECK(ui::areas::layout_fits_areas());
  ImGuiID first_footer = 0;
  ImGuiID second_footer = 0;
  ImGui::DockBuilderSplitNode(env.footer_right, ImGuiDir_Right, 0.5f,
                              &first_footer, &second_footer);
  ImGui::DockBuilderDockWindow(ui::SOFT_KEYBOARD_TITLE, first_footer);
  ImGui::DockBuilderDockWindow(ui::PATCH_LAB_TITLE, second_footer);
  CHECK(!ui::areas::layout_fits_areas());
  ImGui::DockBuilderDockWindow(ui::PATCH_LAB_TITLE, first_footer);
  CHECK(ui::areas::layout_fits_areas());
}

void test_saved_layout_rejects_side_views_outside_the_main_dockspace() {
  TestEnvironment env;
  env.dock_side_views();
  const ImGuiID floating = ImGui::DockBuilderAddNode(0);
  ImGui::DockBuilderSetNodeSize(floating, ImVec2(300, 200));
  ImGui::DockBuilderDockWindow(ui::WAVEFORM_TITLE, floating);
  CHECK(!ui::areas::layout_fits_areas());
  ImGui::DockBuilderDockWindow(ui::WAVEFORM_TITLE, env.footer_left);
  CHECK(ui::areas::layout_fits_areas());
  ImGui::DockBuilderDockWindow(ui::WAVEFORM_TITLE, 0);
  CHECK(!ui::areas::layout_fits_areas());
}

void test_saved_layout_rejects_one_missing_a_side_view() {
  TestEnvironment env;
  CHECK(!ui::areas::layout_fits_areas());
  ImGui::DockBuilderDockWindow(ui::WAVEFORM_TITLE, env.footer_left);
  ImGui::DockBuilderDockWindow(ui::SOFT_KEYBOARD_TITLE, env.footer_right);
  ImGui::DockBuilderDockWindow(ui::PATCH_LAB_TITLE, env.footer_right);
  // Saved under another name, the Library is not this layout's.
  ImGui::DockBuilderDockWindow("Library", env.left);
  CHECK(!ui::areas::layout_fits_areas());
  ImGui::DockBuilderDockWindow(ui::LIBRARY_ID, env.left);
  CHECK(ui::areas::layout_fits_areas());
}

} // namespace

int main() {
  test_editor_names_require_a_complete_stable_id();
  test_saved_layout_accepts_main_splits_and_shared_side_tabs();
  test_saved_layout_rejects_mixed_areas_and_separate_side_tabs();
  test_saved_layout_rejects_side_views_outside_the_main_dockspace();
  test_saved_layout_rejects_one_missing_a_side_view();
  std::cout << "All area tests passed\n";
  return 0;
}
