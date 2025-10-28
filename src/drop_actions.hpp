#pragma once

#include "app_context.hpp"
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

inline void handle_success(AppContext &context, const ym2612::Patch &patch,
                           const std::filesystem::path &path) {
  auto &drop = context.state.ui_state().drop_state;
  if (context.services.patch_session.is_modified()) {
    context.state.ui_state().confirmation_state =
        UIState::ConfirmationState::drop();
    drop.pending_dropped_patch = patch;
    drop.pending_dropped_path = path;
  } else {
    patch_actions::load_dropped_patch(context, patch, path);
    reset_drop_state(drop);
  }
}

inline void handle_multi(AppContext &context,
                         const std::vector<ym2612::Patch> &patches,
                         const std::filesystem::path &path) {
  auto &drop = context.state.ui_state().drop_state;
  drop.instruments = patches;
  drop.pending_instruments_path = path;
  drop.selected_instrument = 0;
  drop.show_picker_for_multiple_instruments = true;
  drop.show_error_popup = false;
  drop.error_message.clear();
}

inline void handle_failure(AppContext &context, const std::string &message) {
  auto &drop = context.state.ui_state().drop_state;
  drop.instruments.clear();
  drop.pending_instruments_path.clear();
  drop.selected_instrument = 0;
  drop.show_picker_for_multiple_instruments = false;
  drop.error_message = message;
  drop.show_error_popup = true;
}

inline void handle_drop(AppContext &context,
                        const std::filesystem::path &path) {
  auto &drop = context.state.ui_state().drop_state;
  drop.error_message.clear();

  const auto result = formats::load_patch_from_file(path);
  switch (result.status) {
  case formats::PatchLoadStatus::Success:
    handle_success(context, result.patches[0], path);
    break;
  case formats::PatchLoadStatus::MultiInstrument:
    handle_multi(context, result.patches, path);
    break;
  case formats::PatchLoadStatus::Failure:
  default:
    handle_failure(context, result.message);
    break;
  }
}

inline void apply_selection(AppContext &context, size_t index) {
  auto &drop = context.state.ui_state().drop_state;
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

  patch_actions::load_dropped_patch(context, patch_to_apply,
                                    drop.pending_instruments_path);
  reset_drop_state(drop);
}

inline void cancel_selection(AppContext &context) {
  reset_drop_state(context.state.ui_state().drop_state);
}

} // namespace drop_actions
