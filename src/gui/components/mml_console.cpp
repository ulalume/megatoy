#include "mml_console.hpp"
#include "formats/ctrmml.hpp"
#include "platform/clipboard.hpp"
#include <cstring>
#include <imgui.h>

namespace ui {

void render_mml_console(const char *title, MmlConsoleContext &context) {
  ImGui::SetNextWindowSize(ImVec2(300, 180), ImGuiCond_FirstUseEver);

  if (!context.ui_prefs.show_mml_console) {
    return;
  }
  if (!ImGui::Begin(title, &context.ui_prefs.show_mml_console)) {
    ImGui::End();
    return;
  }

  auto mml = formats::ctrmml::patch_to_string(context.current_patch());
  auto mml_c = mml.c_str();

  // copy to clipboard button
  if (ImGui::Button("Copy to Clipboard")) {
    platform::clipboard::copy_text(mml_c);
  }
  // panel (border)
  ImGui::BeginChild("MML Panel", ImVec2(0, 0), true);
  ImGui::Text("%s", mml_c);
  ImGui::EndChild();

  ImGui::End();
};

}; // namespace ui
