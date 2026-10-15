#pragma once

#include "app_context.hpp"
#include "app_services.hpp"
#include "app_state.hpp"
#include "core/status.hpp"
#include "history/history_manager.hpp"
#include "history/snapshot_entry.hpp"
#include "patches/patch_session.hpp"
#include <filesystem>
#include <memory>
#include <string>

namespace patch_actions {
namespace detail {
using Snapshot = patches::PatchSession::PatchSnapshot;

/// An undo step that restores into the document it was recorded in.
inline std::unique_ptr<history::HistoryEntry>
make_entry(const std::string &label, const std::string &merge_key,
           patches::DocumentId document, const Snapshot &before,
           const Snapshot &after) {
  return history::make_snapshot_entry<Snapshot>(
      label, merge_key, before, after,
      [document](AppContext &ctx, const Snapshot &snapshot) {
        ctx.services.patch_session.restore_snapshot(document, snapshot);
      });
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
                                  open->capture_snapshot());
      });
}

inline void commit_change(AppContext &context) {
  context.services.history().commit_transaction(context);
  context.services.patch_session.pin_modified_documents();
}

/**
 * Make `document` the active one. An edit still open on the previous one is
 * dropped rather than recorded against the wrong document.
 */
inline void activate_document(AppContext &context,
                              patches::DocumentId document) {
  auto &session = context.services.patch_session;
  if (document == session.active_document_id() ||
      session.find_document(document) == nullptr) {
    return;
  }
  context.services.history().cancel_transaction();
  session.activate(document);
}

/// Bring the document's editor window to the front.
inline void reveal_document(AppContext &context, patches::DocumentId document) {
  context.state.ui_state().reveal_document = document;
}

/// Open a browser entry by the preview rules; `pinned` keeps it open.
inline bool open_entry(AppContext &context, const patches::PatchEntry &entry,
                       bool pinned) {
  context.services.history().cancel_transaction();
  const auto document =
      context.services.patch_session.open_or_activate(entry, pinned);
  if (!document) {
    megatoy::status::error("Failed to load \"" + entry.name + "\"");
    return false;
  }
  reveal_document(context, *document);
  return true;
}

/// Open a browser entry, kept open, in the dock node `dock_id`; 0 places it
/// as open_entry() does. The preview, wherever it is, stays.
inline bool open_entry_in_dock(AppContext &context,
                               const patches::PatchEntry &entry,
                               unsigned int dock_id) {
  context.services.history().cancel_transaction();
  const auto document = context.services.patch_session.open_or_activate(
      entry, /*pinned=*/true, /*reuse_preview=*/false);
  if (!document) {
    megatoy::status::error("Failed to load \"" + entry.name + "\"");
    return false;
  }
  reveal_document(context, *document);
  context.state.ui_state().document_ui(*document).dock_target = dock_id;
  return true;
}

/// Open a patch read from a file outside the browser, as a preview.
inline void open_dropped_patch(AppContext &context, const ym2612::Patch &patch,
                               const std::filesystem::path &source_path) {
  context.services.history().cancel_transaction();
  reveal_document(context,
                  context.services.patch_session.open_or_activate_patch(
                      patch, source_path, /*pinned=*/false));
}

} // namespace patch_actions
