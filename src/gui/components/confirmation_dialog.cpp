#include "confirmation_dialog.hpp"
#include "common.hpp"
#include "modal.hpp"
#include "gui/styles/megatoy_style.hpp"
#include <algorithm>
#include <cstring>
#include <imgui.h>

namespace ui {

void render_confirmation_dialog(ConfirmationDialogContext &context) {
  auto &confirmation_state = context.state;

  // Unsaved changes confirmation dialog. Escape stands in for Cancel; keeping
  // the changes is the answer that loses nothing.
  auto unsaved = begin_modal("Unsaved Changes", ModalDismiss::Escape);
  if (unsaved.dismissed) {
    // The flag has to go with it: the reopen at the bottom of this function
    // would otherwise put the dialog straight back up.
    confirmation_state.show_unsaved_changes_dialog = false;
  }
  if (unsaved.visible) {
    ImGui::TextWrapped("%s", confirmation_state.dialog_message.c_str());
    ImGui::Spacing();

    // "Discard Changes" does not fit the standard width, so the row is
    // measured rather than assumed.
    const float cancel_width = button_width("Cancel");
    const float discard_width = button_width("Discard Changes");
    align_buttons_right({cancel_width, discard_width});
    if (ImGui::Button("Cancel", ImVec2(cancel_width, 0))) {
      confirmation_state.show_unsaved_changes_dialog = false;
      ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Discard Changes", ImVec2(discard_width, 0))) {
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

    end_modal();
  }

  // Open the dialog if requested
  if (confirmation_state.show_unsaved_changes_dialog) {
    ImGui::OpenPopup("Unsaved Changes");
  }

  auto &danger = context.danger_state;
  if (danger.requested && !danger.title.empty()) {
    ImGui::OpenPopup(danger.title.c_str());
    danger.requested = false;
  }

  bool cancel_danger = false;
  bool confirm_danger = false;
  // Escape cancels. Deleting is the destructive answer, so it is the one that
  // has to be asked for explicitly.
  auto danger_modal =
      danger.title.empty()
          ? ModalScope{}
          : begin_modal(danger.title.c_str(), ModalDismiss::Escape);
  cancel_danger = danger_modal.dismissed;
  if (danger_modal.visible) {
    ImGui::TextWrapped("%s", danger.message.c_str());
    ImGui::Spacing();

    // The confirm label is whatever the caller named the action, so it is
    // allowed to outgrow the standard width rather than be clipped by it.
    const float confirm_width = std::max(
        kDialogButtonWidth, button_width(danger.confirm_label.c_str()));
    align_buttons_right({kDialogButtonWidth, confirm_width});
    if (ImGui::Button("Cancel", ImVec2(kDialogButtonWidth, 0))) {
      cancel_danger = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(danger.confirm_label.c_str(), ImVec2(confirm_width, 0))) {
      confirm_danger = true;
      ImGui::CloseCurrentPopup();
    }
    end_modal();
  }

  if (cancel_danger) {
    danger.clear();
  } else if (confirm_danger) {
    auto on_confirm = std::move(danger.on_confirm);
    danger.clear();
    if (on_confirm) {
      on_confirm();
    }
  }

  auto &prompt = context.text_prompt_state;
  if (prompt.requested && !prompt.title.empty()) {
    ImGui::OpenPopup(prompt.title.c_str());
    prompt.requested = false;
  }

  bool cancel_prompt = false;
  bool confirm_prompt = false;
  // Escape cancels, but only once the text field has let go of it -- see
  // escape_pressed() in modal.cpp.
  auto prompt_modal =
      prompt.title.empty()
          ? ModalScope{}
          : begin_modal(prompt.title.c_str(), ModalDismiss::Escape);
  cancel_prompt = prompt_modal.dismissed;
  if (prompt_modal.visible) {
    char input[512];
    std::strncpy(input, prompt.buffer.c_str(), sizeof(input) - 1);
    input[sizeof(input) - 1] = '\0';
    ImGui::SetNextItemWidth(320.0f);
    if (ImGui::InputText(prompt.label.c_str(), input, sizeof(input))) {
      prompt.buffer = input;
    }

    const std::string validation_error =
        prompt.validator ? prompt.validator(prompt.buffer) : std::string{};
    if (!validation_error.empty()) {
      ImGui::TextColored(styles::color(styles::MegatoyCol::StatusWarning), "%s",
                         validation_error.c_str());
    }
    ImGui::Spacing();

    const float prompt_confirm_width = std::max(
        kDialogButtonWidth, button_width(prompt.confirm_label.c_str()));
    align_buttons_right({kDialogButtonWidth, prompt_confirm_width});
    if (ImGui::Button("Cancel", ImVec2(kDialogButtonWidth, 0))) {
      cancel_prompt = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!validation_error.empty());
    if (ImGui::Button(prompt.confirm_label.c_str(),
                      ImVec2(prompt_confirm_width, 0))) {
      confirm_prompt = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    end_modal();
  }

  if (cancel_prompt) {
    prompt.clear();
  } else if (confirm_prompt) {
    const std::string value = prompt.buffer;
    auto on_confirm = std::move(prompt.on_confirm);
    prompt.clear();
    if (on_confirm) {
      on_confirm(value);
    }
  }
}

} // namespace ui
