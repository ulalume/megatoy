#pragma once

#include "app_state.hpp"
#include "formats/patch_loader.hpp"
#include "patch_actions.hpp"

#include <filesystem>

namespace drop_actions {

inline void reset_drop_state(UIState::DropState &drop) {
  drop.instruments.clear();
  drop.pending_instruments_path.clear();
  drop.selected_instrument = 0;
  drop.show_picker_for_multiple_instruments = false;
  drop.show_error_popup = false;
  drop.error_message.clear();
}

inline void handle_success(AppState &state, const ym2612::Patch &patch,
                           const std::filesystem::path &path) {
  auto &drop = state.ui_state().drop_state;
  if (state.patch_session().is_modified()) {
    state.ui_state().confirmation_state = UIState::ConfirmationState::drop();
    drop.pending_dropped_patch = patch;
    drop.pending_dropped_path = path;
  } else {
    patch_actions::load_dropped_patch(state, patch, path);
    reset_drop_state(drop);
  }
}

inline void handle_multi(AppState &state,
                         const std::vector<ym2612::Patch> &patches,
                         const std::filesystem::path &path) {
  auto &drop = state.ui_state().drop_state;
  drop.instruments = patches;
  drop.pending_instruments_path = path;
  drop.selected_instrument = 0;
  drop.show_picker_for_multiple_instruments = true;
  drop.show_error_popup = false;
  drop.error_message.clear();
}

inline void handle_failure(AppState &state, const std::string &message) {
  auto &drop = state.ui_state().drop_state;
  drop.instruments.clear();
  drop.pending_instruments_path.clear();
  drop.selected_instrument = 0;
  drop.show_picker_for_multiple_instruments = false;
  drop.error_message = message;
  drop.show_error_popup = true;
}

inline void handle_drop(AppState &state, const std::filesystem::path &path) {
  auto &drop = state.ui_state().drop_state;
  drop.error_message.clear();

  const auto result = formats::load_patch_from_file(path);
  switch (result.status) {
  case formats::PatchLoadStatus::Success:
    handle_success(state, result.patches[0], path);
    break;
  case formats::PatchLoadStatus::MultiInstrument:
    handle_multi(state, result.patches, path);
    break;
  case formats::PatchLoadStatus::Failure:
  default:
    handle_failure(state, result.message);
    break;
  }
}

inline void apply_selection(AppState &state, size_t index) {
  auto &drop = state.ui_state().drop_state;
  if (index >= drop.instruments.size()) {
    reset_drop_state(drop);
    return;
  }

  auto selected = drop.instruments[index];
  ym2612::Patch patch_to_apply = selected;
  if (!selected.name.empty()) {
    patch_to_apply.name = selected.name;
  } else if (patch_to_apply.name.empty()) {
    patch_to_apply.name = drop.pending_instruments_path.stem().string();
  }

  patch_actions::load_dropped_patch(state, patch_to_apply,
                                    drop.pending_instruments_path);
  reset_drop_state(drop);
}

inline void cancel_selection(AppState &state) {
  reset_drop_state(state.ui_state().drop_state);
}

} // namespace drop_actions
