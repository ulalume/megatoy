#include "confirmation_dialog.hpp"
#include "common.hpp"
#include "gui/styles/megatoy_style.hpp"
#include <cstring>
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

  auto &danger = context.danger_state;
  if (danger.requested && !danger.title.empty()) {
    ImGui::OpenPopup(danger.title.c_str());
    danger.requested = false;
  }

  bool cancel_danger = false;
  bool confirm_danger = false;
  center_next_window();
  if (!danger.title.empty() &&
      ImGui::BeginPopupModal(danger.title.c_str(), nullptr,
                             ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
    force_center_window();
    ImGui::TextWrapped("%s", danger.message.c_str());
    ImGui::Spacing();

    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      cancel_danger = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(danger.confirm_label.c_str(), ImVec2(120, 0))) {
      confirm_danger = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
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
  center_next_window();
  if (!prompt.title.empty() &&
      ImGui::BeginPopupModal(prompt.title.c_str(), nullptr,
                             ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
    force_center_window();

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

    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      cancel_prompt = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!validation_error.empty());
    if (ImGui::Button(prompt.confirm_label.c_str(), ImVec2(120, 0))) {
      confirm_prompt = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
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
