#include "confirmation_dialog.hpp"
#include "common.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include "modal.hpp"
#include "patches/filename_utils.hpp"
#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <string>

namespace ui {

namespace {

/**
 * "New Patch": a name and one of the formats megatoy can write, on one row.
 *
 * Confirm is refused while the name is one the folder cannot take -- empty,
 * unusable, or already there in the chosen format -- and says which.
 */
void render_new_patch_prompt(UIState::NewPatchPromptState &prompt) {
  constexpr const char *kTitle = "New Patch";
  if (prompt.requested) {
    ImGui::OpenPopup(kTitle);
    prompt.requested = false;
  }

  auto modal = begin_modal(kTitle, ModalDismiss::Escape);
  bool cancelled = modal.dismissed;
  bool confirmed = false;
  if (modal.visible) {
    // The combo is sized to the longest name it has to show; the field takes
    // what is left of the row.
    float combo_width = 0.0f;
    for (const auto &format : prompt.formats) {
      combo_width = std::max(
          combo_width, ImGui::CalcTextSize(format.display_name().c_str()).x);
    }
    combo_width += ImGui::GetFrameHeight() +
                   ImGui::GetStyle().FramePadding.x * 2.0f;

    char input[512];
    std::strncpy(input, prompt.name.c_str(), sizeof(input) - 1);
    input[sizeof(input) - 1] = '\0';
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - combo_width -
                            ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::InputTextWithHint("##NewPatchName", "Name", input,
                                 sizeof(input))) {
      prompt.name = input;
    }

    ImGui::SameLine();
    std::string preview = prompt.extension;
    for (const auto &format : prompt.formats) {
      if (format.extension == prompt.extension) {
        preview = format.display_name();
        break;
      }
    }
    ImGui::SetNextItemWidth(combo_width);
    if (ImGui::BeginCombo("##NewPatchFormat", preview.c_str())) {
      for (const auto &format : prompt.formats) {
        const bool selected = format.extension == prompt.extension;
        if (ImGui::Selectable(format.display_name().c_str(), selected)) {
          prompt.extension = format.extension;
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    const std::string validation_error = patches::new_patch_name_error(
        prompt.name, prompt.extension, prompt.folder);
    // The empty field the dialog opens with is not a mistake the user has
    // made yet -- its hint already says what belongs there. Create stays
    // disabled either way.
    if (!validation_error.empty() && !prompt.name.empty()) {
      ImGui::TextColored(styles::color(styles::MegatoyCol::StatusWarning), "%s",
                         validation_error.c_str());
    }
    ImGui::Spacing();

    const float create_width =
        std::max(dialog_button_width(), button_width("Create"));
    align_buttons_right({dialog_button_width(), create_width});
    if (ImGui::Button("Cancel", ImVec2(dialog_button_width(), 0))) {
      cancelled = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!validation_error.empty());
    if (ImGui::Button("Create", ImVec2(create_width, 0))) {
      confirmed = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    end_modal();
  }

  if (cancelled) {
    prompt.clear();
  } else if (confirmed) {
    const std::string name = prompt.name;
    const std::string extension = prompt.extension;
    auto on_confirm = std::move(prompt.on_confirm);
    prompt.clear();
    if (on_confirm) {
      on_confirm(name, extension);
    }
  }
}

} // namespace

void render_confirmation_dialog(ConfirmationDialogContext &context) {
  auto &confirmation_state = context.state;

  // Unsaved changes confirmation dialog. Escape stands in for Cancel; keeping
  // the changes is the answer that loses nothing.
  auto unsaved = begin_modal("Unsaved Changes", ModalDismiss::Escape);
  if (unsaved.dismissed) {
    // The flag has to go with it: the reopen at the bottom of this function
    // would otherwise put the dialog straight back up.
    confirmation_state.show_unsaved_changes_dialog = false;
    if (confirmation_state.operation ==
            UIState::ConfirmationState::Operation::Close &&
        context.keep_open) {
      context.keep_open(confirmation_state.pending_document);
    }
  }
  if (unsaved.visible) {
    ImGui::TextWrapped("%s", confirmation_state.dialog_message.c_str());
    ImGui::Spacing();

    const bool closing = confirmation_state.operation ==
                         UIState::ConfirmationState::Operation::Close;
    const auto finish = [&confirmation_state]() {
      confirmation_state.show_unsaved_changes_dialog = false;
      ImGui::CloseCurrentPopup();
    };
    // The labels do not all fit the standard width, so the row is measured
    // rather than assumed. Discarding stands apart at the left, away from
    // the answers that keep the changes.
    const auto document = confirmation_state.pending_document;
    const bool offers_save =
        patches::confirmation_offers_save(confirmation_state);
    const float discard_width = button_width("Discard Changes");
    const float cancel_width = button_width("Cancel");
    const float save_width =
        std::max(dialog_button_width(), button_width("Save"));
    if (ImGui::Button("Discard Changes", ImVec2(discard_width, 0))) {
      finish();
      if (closing) {
        if (context.discard_and_close) {
          context.discard_and_close(document);
        }
      } else if (context.confirm_exit) {
        context.confirm_exit();
      }
    }
    ImGui::SameLine(0.0f, ui::scale::px(24.0f));
    if (offers_save) {
      align_buttons_right({cancel_width, save_width});
    } else {
      align_buttons_right({cancel_width});
    }
    if (ImGui::Button("Cancel", ImVec2(cancel_width, 0))) {
      finish();
      if (closing && context.keep_open) {
        context.keep_open(document);
      }
    }
    if (offers_save) {
      ImGui::SameLine();
      if (ImGui::Button("Save", ImVec2(save_width, 0))) {
        finish();
        if (closing) {
          if (context.save_and_close) {
            context.save_and_close(document);
          }
        } else if (context.save_and_exit) {
          context.save_and_exit(document);
        }
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
  auto danger_modal = danger.title.empty() ? ModalScope{}
                                           : begin_modal(danger.title.c_str(),
                                                         ModalDismiss::Escape);
  cancel_danger = danger_modal.dismissed;
  if (danger_modal.visible) {
    ImGui::TextWrapped("%s", danger.message.c_str());
    ImGui::Spacing();

    // The confirm label is whatever the caller named the action, so it is
    // allowed to outgrow the standard width rather than be clipped by it.
    const float confirm_width = std::max(
        dialog_button_width(), button_width(danger.confirm_label.c_str()));
    align_buttons_right({dialog_button_width(), confirm_width});
    if (ImGui::Button("Cancel", ImVec2(dialog_button_width(), 0))) {
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
  auto prompt_modal = prompt.title.empty() ? ModalScope{}
                                           : begin_modal(prompt.title.c_str(),
                                                         ModalDismiss::Escape);
  cancel_prompt = prompt_modal.dismissed;
  if (prompt_modal.visible) {
    char input[512];
    std::strncpy(input, prompt.buffer.c_str(), sizeof(input) - 1);
    input[sizeof(input) - 1] = '\0';
    // Fills the row, less the label ImGui puts to its right.
    const float label_width = ImGui::CalcTextSize(prompt.label.c_str()).x;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - label_width -
                            ImGui::GetStyle().ItemInnerSpacing.x);
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
        dialog_button_width(), button_width(prompt.confirm_label.c_str()));
    align_buttons_right({dialog_button_width(), prompt_confirm_width});
    if (ImGui::Button("Cancel", ImVec2(dialog_button_width(), 0))) {
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

  render_new_patch_prompt(context.new_patch_prompt_state);
}

} // namespace ui
