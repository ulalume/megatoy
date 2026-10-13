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
#include <memory>
#include <string>

namespace patch_actions {
namespace detail {
using Snapshot = patches::PatchSession::PatchSnapshot;

/// An undo step that restores into the document it was recorded in.
inline std::unique_ptr<history::HistoryEntry>
make_entry(const std::string &label, const std::string &merge_key,
           patches::DocumentId document, const Snapshot &before,
           const Snapshot &after, bool announce) {
  return history::make_snapshot_entry<Snapshot>(
      label, merge_key, before, after,
      [document, announce](AppContext &ctx, const Snapshot &snapshot) {
        ctx.services.patch_session.restore_snapshot(document, snapshot);
        if (announce) {
          std::cout << "Patch restored" << std::endl;
        }
      });
}

inline void record_change(AppContext &context, const std::string &label,
                          const Snapshot &before, const Snapshot &after) {
  const auto document = context.services.patch_session.active_document_id();
  context.services.history().begin_transaction(
      label, {}, [label, document, before, after](AppContext &) {
        return make_entry(label, {}, document, before, after,
                          /*announce=*/true);
      });
  context.services.history().commit_transaction(context);
}
} // namespace detail

/**
 * Open one undo step on the active document: the state now is what undo
 * returns to, the state at commit_change() what redo restores.
 */
inline void begin_change(AppContext &context, const std::string &label,
                         const std::string &merge_key) {
  auto &session = context.services.patch_session;
  const auto document = session.active_document_id();
  auto before = session.capture_snapshot();
  context.services.history().begin_transaction(
      label, merge_key,
      [label, merge_key, document, before = std::move(before)](
          AppContext &ctx) -> std::unique_ptr<history::HistoryEntry> {
        const auto *open = ctx.services.patch_session.find_document(document);
        if (open == nullptr) {
          return nullptr;
        }
        return detail::make_entry(label, merge_key, document, before,
                                  open->capture_snapshot(),
                                  /*announce=*/false);
      });
}

inline void commit_change(AppContext &context) {
  context.services.history().commit_transaction(context);
}

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
