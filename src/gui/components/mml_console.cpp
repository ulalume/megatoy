#include "mml_console.hpp"
#include "formats/ctrmml.hpp"
#include "platform/platform_config.hpp"
#include <cstring>
#include <imgui.h>
#if defined(MEGATOY_PLATFORM_WEB)
#include <emscripten.h>
#endif

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
#if defined(MEGATOY_PLATFORM_WEB)
    EM_ASM({ var txt = UTF8ToString($0); navigator.clipboard.writeText(txt).catch(console.warn); }, mml_c);
#else
    ImGui::SetClipboardText(mml_c);
#endif
  }
  // panel (border)
  ImGui::BeginChild("MML Panel", ImVec2(0, 0), true);
  ImGui::Text("%s", mml_c);
  ImGui::EndChild();

  ImGui::End();
};

}; // namespace ui
