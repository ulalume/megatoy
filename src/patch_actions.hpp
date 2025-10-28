#pragma once

#include "app_state.hpp"
#include "history/snapshot_entry.hpp"
#include <filesystem>
#include <iostream>
#include <string>

namespace patch_actions {
namespace detail {
inline patches::PatchSession::PatchSnapshot capture(AppState &state) {
  return state.patch_session().capture_snapshot();
}

inline void record_change(AppState &state, const std::string &label,
                          const patches::PatchSession::PatchSnapshot &before,
                          const patches::PatchSession::PatchSnapshot &after) {
  state.history().begin_transaction(
      label, {}, [label, before, after](AppState &target) {
        return history::make_snapshot_entry<
            patches::PatchSession::PatchSnapshot>(
            label, std::string{}, before, after,
            [](AppState &app,
               const patches::PatchSession::PatchSnapshot &snapshot) {
              app.patch_session().restore_snapshot(snapshot);
              std::cout << "Patch restored" << std::endl;
            });
      });
  state.history().commit_transaction(state);
}
} // namespace detail

inline bool load(AppState &state, const patches::PatchEntry &patch_info) {
  const auto before = detail::capture(state);

  ym2612::Patch loaded_patch;
  if (!state.patch_session().repository().load_patch(patch_info,
                                                     loaded_patch)) {
    std::cerr << "Failed to load preset patch: " << patch_info.name
              << std::endl;
    return false;
  }

  state.patch_session().current_patch() = loaded_patch;
  state.patch_session().set_current_patch_path(patch_info.relative_path);
  state.patch_session().apply_patch_to_audio();

  const auto after = detail::capture(state);
  detail::record_change(state, "Load: " + patch_info.name, before, after);
  state.patch_session().mark_as_clean();
  std::cout << "Loaded preset patch: " << patch_info.name << std::endl;
  return true;
}

inline void safe_load(AppState &state, const patches::PatchEntry &preset_info) {
  if (state.patch_session().is_modified()) {
    state.ui_state().confirmation_state =
        UIState::ConfirmationState::load(preset_info);
  } else {
    load(state, preset_info);
  }
}

inline void load_dropped_patch(AppState &state, const ym2612::Patch &patch,
                               const std::filesystem::path &source_path) {
  const auto before = detail::capture(state);
  state.patch_session().set_current_patch(patch, source_path);
  const auto after = detail::capture(state);
  detail::record_change(state, "Load: " + source_path.filename().string(),
                        before, after);
}

} // namespace patch_actions
