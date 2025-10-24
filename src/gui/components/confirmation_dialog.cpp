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
      confirmation_state.show_unsaved_changes_dialog = false;
      ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Discard Changes")) {
      // Handle different confirmation types

      switch (confirmation_state.operation) {
      case UIState::ConfirmationState::Operation::Load:
        app_state.load_patch(confirmation_state.pending_patch_entry);
        confirmation_state.show_unsaved_changes_dialog = false;
        ImGui::CloseCurrentPopup();
        break;
      case UIState::ConfirmationState::Operation::Drop: {
        auto &drop_state = app_state.ui_state().drop_state;
        app_state.load_dropped_patch_with_history(
            drop_state.pending_dropped_patch, drop_state.pending_dropped_path);
        confirmation_state.show_unsaved_changes_dialog = false;
        ImGui::CloseCurrentPopup();
        break;
      }
      case UIState::ConfirmationState::Operation::Exit:
        app_state.gui().set_should_close(true);
        app_state.patch_session().mark_as_clean();
        confirmation_state.show_unsaved_changes_dialog = false;
        ImGui::CloseCurrentPopup();
        break;
      }
    }

    ImGui::EndPopup();
  }

  // Open the dialog if requested
  if (confirmation_state.show_unsaved_changes_dialog) {
    ImGui::OpenPopup("Unsaved Changes");
  }
}

} // namespace ui
