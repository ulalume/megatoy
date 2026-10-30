#include "history_shortcuts.hpp"
#include "app_context.hpp"
#include "app_services.hpp"
#include <imgui.h>

namespace ui {

void handle_history_shortcuts(AppContext &app_context) {
  auto &history = app_context.services.history();
  auto &io = ImGui::GetIO();
  if (io.WantTextInput) {
    return;
  }

  const bool primary_modifier = io.KeyCtrl || io.KeySuper;
  if (!primary_modifier) {
    return;
  }

  const bool shift = io.KeyShift;

  if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
    if (shift) {
      if (history.can_redo()) {
        history.redo(app_context);
      }
    } else if (history.can_undo()) {
      history.undo(app_context);
    }
  } else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
    if (history.can_redo()) {
      history.redo(app_context);
    }
  }
}

} // namespace ui
