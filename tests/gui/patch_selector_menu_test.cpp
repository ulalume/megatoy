// The Library's context menu, driven through ImGui with a pointer: what it
// starts runs once the tree is drawn, because the tree's rows point into the
// repository's tree and a write to the folder rebuilds it.

#include "../patches/session_test_environment.hpp"
#include "gui/components/patch_selector.hpp"
#include "gui/components/patch_tree_view.hpp"

#include <functional>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <string>

namespace {

using session_tests::LaunchedEnvironment;

struct Gui {
  Gui() {
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1600, 900);
    io.DeltaTime = 1.0f / 60.0f;
    io.ConfigInputTrickleEventQueue = false;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    unsigned char *pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  }
  ~Gui() { ImGui::DestroyContext(); }
};

void frame(ui::PatchSelectorContext &context) {
  ImGui::NewFrame();
  ui::render_patch_selector("Library", context);
  ImGui::EndFrame();
}

/// The pointer at `pos`, for long enough that what is under it is hovered.
void point_at(ui::PatchSelectorContext &context, const ImVec2 &pos) {
  ImGui::GetIO().AddMousePosEvent(pos.x, pos.y);
  frame(context);
  frame(context);
}

void click(ui::PatchSelectorContext &context, ImGuiMouseButton button) {
  ImGui::GetIO().AddMouseButtonEvent(button, true);
  frame(context);
  ImGui::GetIO().AddMouseButtonEvent(button, false);
  frame(context);
  frame(context);
}

ImGuiWindow *window_named(const char *part) {
  for (ImGuiWindow *window : GImGui->Windows) {
    if (window->WasActive && std::string(window->Name).find(part) !=
                                 std::string::npos) {
      return window;
    }
  }
  return nullptr;
}

/// Moves the pointer down `window` until `found` says it is where wanted.
bool scan_down(ui::PatchSelectorContext &context, const ImGuiWindow *window,
               const std::function<bool()> &found) {
  const float x = window->Pos.x + window->Size.x * 0.5f;
  for (float y = window->Pos.y + 1.0f; y < window->Pos.y + window->Size.y;
       y += 3.0f) {
    point_at(context, ImVec2(x, y));
    if (found()) {
      return true;
    }
  }
  return false;
}

void test_duplicate_runs_once_the_tree_is_drawn() {
  LaunchedEnvironment env("patch_selector_menu_test");
  const auto file = env.folder / "violin.dmp";
  CHECK(patches::write_patch(env.session.current_patch(), file));
  auto &repository = env.session.repository();
  repository.refresh();
  CHECK(!repository.tree().empty());

  Gui gui;
  PreferenceManager::UIPreferences prefs;
  prefs.show_patch_selector = true;
  ui::PatchSelectorContext context{repository, env.session, prefs};

  int duplicated = 0;
  context.duplicate_patch = [&](const patches::PatchEntry &entry) {
    ++duplicated;
    // Neither the menu nor the tree is being drawn.
    CHECK((ImGui::GetCurrentWindow()->Flags &
           (ImGuiWindowFlags_Popup | ImGuiWindowFlags_ChildWindow)) == 0);
    // The copy's write rebuilds the tree; the entry outlives it.
    repository.refresh();
    CHECK(entry.full_path == file);
    CHECK(entry.name == "violin");
  };

  ui::selector_detail::open_tree_directory(
      repository.tree().front().relative_path);
  frame(context);
  frame(context);

  // The folder's heading is the first row under the pointer, the patch the
  // next.
  const ImGuiWindow *tree = window_named("PresetTree");
  CHECK(tree != nullptr);
  ImGuiID heading = 0;
  CHECK(scan_down(context, tree, [&]() {
    const ImGuiID hovered = GImGui->HoveredId;
    if (hovered == 0) {
      return false;
    }
    if (heading == 0) {
      heading = hovered;
    }
    return hovered != heading;
  }));

  click(context, ImGuiMouseButton_Right);
  CHECK(!GImGui->OpenPopupStack.empty());
  ImGuiWindow *menu = GImGui->OpenPopupStack.back().Window;
  CHECK(menu != nullptr);
  const ImGuiID item = menu->GetID("Duplicate...");
  CHECK(scan_down(context, menu,
                  [&]() { return GImGui->HoveredId == item; }));
  CHECK(duplicated == 0);

  click(context, ImGuiMouseButton_Left);
  CHECK(duplicated == 1);
  CHECK(!context.pending_duplicate.has_value());
}

} // namespace

int main() {
  test_duplicate_runs_once_the_tree_is_drawn();
  std::cout << "All patch selector menu tests passed\n";
  return 0;
}
