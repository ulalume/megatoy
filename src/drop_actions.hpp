#pragma once

#include "app_state.hpp"
#include <filesystem>
#include <functional>

struct AppServices;

namespace drop_actions {

using ApplyPatchFn =
    std::function<void(const ym2612::Patch &, const std::filesystem::path &)>;

struct Environment {
  AppServices &services;
  UIState &ui_state;
  ApplyPatchFn apply_patch;
};

void reset_drop_state(UIState::DropState &drop);
void handle_drop(Environment &env, const std::filesystem::path &path);
void apply_selection(Environment &env, size_t index);
void cancel_selection(UIState::DropState &drop);

} // namespace drop_actions
