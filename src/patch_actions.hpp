#pragma once

#include "app_context.hpp"
#include "app_services.hpp"
#include "app_state.hpp"
#include "core/status.hpp"
#include "history/history_manager.hpp"
#include "history/snapshot_entry.hpp"
#include "patches/patch_session.hpp"
#include <filesystem>
#include <iostream>
#include <string>

namespace patch_actions {
namespace detail {
inline void record_change(AppContext &context, const std::string &label,
                          const patches::PatchSession::PatchSnapshot &before,
                          const patches::PatchSession::PatchSnapshot &after) {
  context.services.history.begin_transaction(
      label, {}, [label, before, after](AppContext &ctx) {
        return history::make_snapshot_entry<
            patches::PatchSession::PatchSnapshot>(
            label, std::string{}, before, after,
            [](AppContext &ctx,
               const patches::PatchSession::PatchSnapshot &snapshot) {
              ctx.services.patch_session.restore_snapshot(snapshot);
              std::cout << "Patch restored" << std::endl;
            });
      });
  context.services.history.commit_transaction(context);
}
} // namespace detail

inline bool load(AppContext &context, const patches::PatchEntry &patch_info) {
  auto &patch_session = context.services.patch_session;
  const auto before = patch_session.capture_snapshot();

  if (!patch_session.load_patch_from_entry(patch_info)) {
    megatoy::status::error("Failed to load \"" + patch_info.name + "\"");
    return false;
  }

  const auto after = patch_session.capture_snapshot();
  detail::record_change(context, "Load: " + patch_info.name, before, after);
  patch_session.mark_as_clean();
  std::cout << "Loaded preset patch: " << patch_info.name << std::endl;
  return true;
}

inline void safe_load(AppContext &context,
                      const patches::PatchEntry &preset_info) {
  auto &patch_session = context.services.patch_session;
  if (patch_session.is_modified()) {
    context.state.ui_state().confirmation_state =
        UIState::ConfirmationState::load(preset_info);
  } else {
    load(context, preset_info);
  }
}

inline void load_dropped_patch(AppContext &context, const ym2612::Patch &patch,
                               const std::filesystem::path &source_path) {
  auto &patch_session = context.services.patch_session;
  const auto before = patch_session.capture_snapshot();
  patch_session.set_current_patch(patch, source_path);
  const auto after = patch_session.capture_snapshot();
  detail::record_change(context, "Load: " + source_path.filename().string(),
                        before, after);
}

} // namespace patch_actions
