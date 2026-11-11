#pragma once

#include "app_state.hpp"
#include <filesystem>
#include <functional>

struct AppServices;

namespace drop_actions {

using ApplyPatchFn = UIState::DropState::ApplyPatch;

struct Environment {
  AppServices &services;
  UIState &ui_state;
  ApplyPatchFn apply_patch;
};

void reset_drop_state(UIState::DropState &drop);
void handle_drop(Environment &env, const std::filesystem::path &path);
/// A file dropped onto one place: its patch goes to `apply`, after a pick
/// where it holds several.
void handle_drop_into(Environment &env, const std::filesystem::path &path,
                      ApplyPatchFn apply);
void apply_selection(Environment &env, size_t index);
void cancel_selection(UIState::DropState &drop);

} // namespace drop_actions
