#pragma once

#include "app_state.hpp"
#include "patches/patch_repository.hpp"
#include "ym2612/patch.hpp"
#include <filesystem>
#include <functional>

namespace ui {

struct ConfirmationDialogContext {
  UIState::ConfirmationState &state;
  UIState::DropState &drop_state;
  std::function<void(const patches::PatchEntry &)> load_patch_entry;
  std::function<void(const ym2612::Patch &, const std::filesystem::path &)>
      apply_dropped_patch;
  std::function<void()> confirm_exit;
};

void render_confirmation_dialog(ConfirmationDialogContext &context);

} // namespace ui
