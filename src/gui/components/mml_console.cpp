#include "mml_console.hpp"
#include "formats/ctrmml.hpp"
#include <cstring>
#include <imgui.h>

namespace ui {

void render_mml_console(AppState &app_state) {
  auto &ui_state = app_state.ui_state();
  ImGui::SetNextWindowSize(ImVec2(300, 180), ImGuiCond_FirstUseEver);

  if (!ui_state.prefs.show_mml_console) {
    return;
  }
  if (!ImGui::Begin("MML Console", &ui_state.prefs.show_mml_console)) {
    ImGui::End();
    return;
  }

  auto mml = ym2612::formats::ctrmml::patch_to_string(app_state.patch());
  auto mml_c = mml.c_str();

  // copy to clipboard button
  if (ImGui::Button("Copy to Clipboard")) {
    ImGui::SetClipboardText(mml_c);
  }
  // panel (border)
  ImGui::BeginChild("MML Panel", ImVec2(0, 0), true);
  ImGui::Text("%s", mml_c);
  ImGui::EndChild();

  ImGui::End();
};

}; // namespace ui
