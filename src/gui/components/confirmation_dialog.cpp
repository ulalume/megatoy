#include "confirmation_dialog.hpp"
#include "common.hpp"
#include <imgui.h>

namespace ui {

void render_confirmation_dialog(ConfirmationDialogContext &context) {
  auto &confirmation_state = context.state;

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
        if (context.load_patch_entry) {
          context.load_patch_entry(confirmation_state.pending_patch_entry);
        }
        confirmation_state.show_unsaved_changes_dialog = false;
        ImGui::CloseCurrentPopup();
        break;
      case UIState::ConfirmationState::Operation::Drop: {
        if (context.apply_dropped_patch) {
          context.apply_dropped_patch(context.drop_state.pending_dropped_patch,
                                      context.drop_state.pending_dropped_path);
        }
        confirmation_state.show_unsaved_changes_dialog = false;
        ImGui::CloseCurrentPopup();
        break;
      }
      case UIState::ConfirmationState::Operation::Exit:
        if (context.confirm_exit) {
          context.confirm_exit();
        }
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
