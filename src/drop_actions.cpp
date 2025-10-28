#include "drop_actions.hpp"

#include "app_services.hpp"
#include "formats/patch_loader.hpp"
#include <utility>
#include <vector>

namespace drop_actions {
namespace {

void handle_success(Environment &env, ym2612::Patch patch,
                    const std::filesystem::path &path) {
  auto &drop = env.ui_state.drop_state;
  if (env.services.patch_session.is_modified()) {
    env.ui_state.confirmation_state = UIState::ConfirmationState::drop();
    drop.pending_dropped_patch = std::move(patch);
    drop.pending_dropped_path = path;
  } else {
    if (env.apply_patch) {
      env.apply_patch(patch, path);
    }
    reset_drop_state(drop);
  }
}

void handle_multi(Environment &env, std::vector<ym2612::Patch> patches,
                  const std::filesystem::path &path) {
  auto &drop = env.ui_state.drop_state;
  drop.instruments = std::move(patches);
  drop.pending_instruments_path = path;
  drop.selected_instrument = 0;
  drop.show_picker_for_multiple_instruments = true;
  drop.show_error_popup = false;
  drop.error_message.clear();
}

void handle_failure(Environment &env, const std::string &message) {
  auto &drop = env.ui_state.drop_state;
  drop.instruments.clear();
  drop.pending_instruments_path.clear();
  drop.selected_instrument = 0;
  drop.show_picker_for_multiple_instruments = false;
  drop.error_message = message;
  drop.show_error_popup = true;
}

} // namespace

void reset_drop_state(UIState::DropState &drop) {
  drop.instruments.clear();
  drop.pending_instruments_path.clear();
  drop.selected_instrument = 0;
  drop.show_picker_for_multiple_instruments = false;
  drop.show_error_popup = false;
  drop.error_message.clear();
}

void handle_drop(Environment &env, const std::filesystem::path &path) {
  auto &drop = env.ui_state.drop_state;
  drop.error_message.clear();

  auto result = formats::load_patch_from_file(path);
  switch (result.status) {
  case formats::PatchLoadStatus::Success:
    if (!result.patches.empty()) {
      handle_success(env, std::move(result.patches[0]), path);
    } else {
      handle_failure(env, "No instruments found.");
    }
    break;
  case formats::PatchLoadStatus::MultiInstrument:
    handle_multi(env, std::move(result.patches), path);
    break;
  case formats::PatchLoadStatus::Failure:
  default:
    handle_failure(env, result.message);
    break;
  }
}

void apply_selection(Environment &env, size_t index) {
  auto &drop = env.ui_state.drop_state;
  if (index >= drop.instruments.size()) {
    reset_drop_state(drop);
    return;
  }

  ym2612::Patch patch_to_apply = drop.instruments[index];
  if (patch_to_apply.name.empty()) {
    patch_to_apply.name = drop.pending_instruments_path.stem().string();
  }

  if (env.apply_patch) {
    env.apply_patch(patch_to_apply, drop.pending_instruments_path);
  }

  reset_drop_state(drop);
}

void cancel_selection(UIState::DropState &drop) { reset_drop_state(drop); }

} // namespace drop_actions
