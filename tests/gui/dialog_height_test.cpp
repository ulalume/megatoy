// A dialog's warning comes and goes as its field is typed into; the dialog
// keeps its height through it.

#include "../test_check.hpp"
#include "gui/components/confirmation_dialog.hpp"

#include <filesystem>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <string>

namespace {

struct Gui {
  Gui() {
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1600, 900);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char *pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  }
  ~Gui() { ImGui::DestroyContext(); }
};

struct Dialogs {
  UIState::ConfirmationState confirmation;
  UIState::DangerConfirmationState danger;
  UIState::TextPromptState text_prompt;
  UIState::NewPatchPromptState new_patch;
  ui::ConfirmationDialogContext context{confirmation, danger, text_prompt,
                                        new_patch,    {},     {},
                                        {},           {},     {}};

  /// The open dialog's height, once it has settled.
  float height() {
    for (int frame = 0; frame < 4; ++frame) {
      ImGui::NewFrame();
      ui::render_confirmation_dialog(context);
      ImGui::EndFrame();
    }
    CHECK(!GImGui->OpenPopupStack.empty());
    const ImGuiWindow *window = GImGui->OpenPopupStack.back().Window;
    CHECK(window != nullptr);
    return window->Size.y;
  }
};

void test_new_patch_dialog_keeps_its_height() {
  Gui gui;
  Dialogs dialogs;
  dialogs.new_patch.request(std::filesystem::temp_directory_path(),
                            {{".dmp", "DefleMask Preset", false}}, {});
  const float empty = dialogs.height();

  dialogs.new_patch.name = "lead";
  CHECK(dialogs.height() == empty);

  dialogs.new_patch.name = "le/ad";
  CHECK(dialogs.height() == empty);
}

void test_text_prompt_keeps_its_height() {
  Gui gui;
  Dialogs dialogs;
  dialogs.text_prompt.request(
      "Rename", "Name", "lead", "Rename", {}, [](const std::string &value) {
        return value == "lead" ? std::string() : std::string("Not that.");
      });
  const float accepted = dialogs.height();

  dialogs.text_prompt.buffer = "bass";
  CHECK(dialogs.height() == accepted);
}

} // namespace

int main() {
  test_new_patch_dialog_keeps_its_height();
  test_text_prompt_keeps_its_height();
  std::cout << "All dialog height tests passed\n";
  return 0;
}
