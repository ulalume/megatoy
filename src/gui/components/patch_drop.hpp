#pragma once

#include "app_state.hpp"
#include <functional>

namespace ui {

struct PatchDropContext {
  UIState::DropState &drop_state;
  std::function<void()> cancel_selection;
  std::function<void(size_t)> apply_selection;
};

void render_patch_drop_feedback(PatchDropContext &context);

} // namespace ui
