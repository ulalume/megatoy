#pragma once

#include "app_state.hpp"
#include "patches/patch_document.hpp"
#include <functional>

namespace ui {

struct ConfirmationDialogContext {
  UIState::ConfirmationState &state;
  UIState::DangerConfirmationState &danger_state;
  UIState::TextPromptState &text_prompt_state;
  UIState::NewPatchPromptState &new_patch_prompt_state;
  /// Close a document without saving it.
  std::function<void(patches::DocumentId)> discard_and_close;
  /// Save a document, then close it once it has nothing left unsaved.
  std::function<void(patches::DocumentId)> save_and_close;
  std::function<void()> confirm_exit;
};

void render_confirmation_dialog(ConfirmationDialogContext &context);

} // namespace ui
