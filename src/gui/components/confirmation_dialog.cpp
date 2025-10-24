#include "confirmation_dialog.hpp"
#include "common.hpp"
#include <imgui.h>

namespace ui {

void render_confirmation_dialog(AppState &app_state) {
  auto &confirmation_state = app_state.ui_state().confirmation_state;

  // Unsaved changes confirmation dialog
  center_next_window();
  if (ImGui::BeginPopupModal("Unsaved Changes", nullptr,
                             ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
    force_center_window();

    ImGui::TextWrapped("%s", confirmation_state.dialog_message.c_str());
    ImGui::Spacing();

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      // Cancel the operation
      confirmation_state.show_unsaved_changes_dialog = false;
      ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Discard Changes")) {
      // Load new patch without saving
      if (confirmation_state.is_drop_confirmation) {
        auto &drop_state = app_state.ui_state().drop_state;
        app_state.load_dropped_patch_with_history(
            drop_state.pending_dropped_patch, drop_state.pending_dropped_path);
      } else {
        app_state.load_patch(confirmation_state.pending_patch_entry);
      }
      confirmation_state.show_unsaved_changes_dialog = false;
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

  // Open the dialog if requested
  if (confirmation_state.show_unsaved_changes_dialog) {
    ImGui::OpenPopup("Unsaved Changes");
    confirmation_state.show_unsaved_changes_dialog = false; // Reset flag
  }
}

} // namespace ui
