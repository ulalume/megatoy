#pragma once

#include "app_context.hpp"
#include "app_services.hpp"
#include "app_state.hpp"
#include "core/status.hpp"
#include "history/history_manager.hpp"
#include "history/snapshot_entry.hpp"
#include "patches/patch_session.hpp"
#include "platform/host.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace patch_actions {
namespace detail {
inline void sync_patch_url(AppContext &context) {
  platform::host::current_patch_changed(
      context.services.patch_session.current_patch());
}

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
        sync_patch_url(ctx);
      });
}

/// Makes the undo step for a change to `document` that began at `before`,
/// once the change is over; none if the document has been closed.
inline auto step_from(std::string label, std::string merge_key,
                      patches::DocumentId document, Snapshot before) {
  return [label = std::move(label), merge_key = std::move(merge_key), document,
          before = std::move(before)](
             AppContext &ctx) -> std::unique_ptr<history::HistoryEntry> {
    const auto *open = ctx.services.patch_session.find_document(document);
    if (open == nullptr) {
      return nullptr;
    }
    return make_entry(label, merge_key, document, before,
                      open->capture_snapshot());
  };
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
      detail::step_from(label, merge_key, document, std::move(before)));
}

inline void commit_change(AppContext &context) {
  if (context.services.history().commit_transaction(context)) {
    detail::sync_patch_url(context);
  }
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

/// Open an untitled patch and show its editor.
inline void new_patch(AppContext &context) {
  context.services.history().cancel_transaction();
  reveal_document(context,
                  context.services.patch_session.open_new_document());
}

namespace detail {
/// Opens a browser entry and shows it; says so when it cannot be read.
inline std::optional<patches::DocumentId>
open_and_reveal(AppContext &context, const patches::PatchEntry &entry,
                bool pinned, bool reuse_preview) {
  context.services.history().cancel_transaction();
  const auto document = context.services.patch_session.open_or_activate(
      entry, pinned, reuse_preview);
  if (!document) {
    megatoy::status::error("Failed to load \"" + entry.name + "\"");
    return std::nullopt;
  }
  reveal_document(context, *document);
  return document;
}
} // namespace detail

/// Open a browser entry by the preview rules; `pinned` keeps it open.
inline bool open_entry(AppContext &context, const patches::PatchEntry &entry,
                       bool pinned) {
  return detail::open_and_reveal(context, entry, pinned,
                                 /*reuse_preview=*/true)
      .has_value();
}

/// Open a browser entry, kept open, in the dock node `dock_id`; 0 places it
/// as open_entry() does. The preview, wherever it is, stays.
inline bool open_entry_in_dock(AppContext &context,
                               const patches::PatchEntry &entry,
                               unsigned int dock_id) {
  const auto document = detail::open_and_reveal(
      context, entry, /*pinned=*/true, /*reuse_preview=*/false);
  if (!document) {
    return false;
  }
  context.state.ui_state().document_ui(*document).dock_target = dock_id;
  return true;
}

/**
 * Show what the document's file now holds, as one step undo takes back.
 * Undone, the contents differ from the file and so count as unsaved.
 */
inline void reload_from_disk(AppContext &context, patches::DocumentId document,
                             const ym2612::Patch &patch) {
  auto &session = context.services.patch_session;
  const auto *open = session.find_document(document);
  if (open == nullptr) {
    return;
  }
  if (document == session.active_document_id()) {
    context.services.history().cancel_transaction();
  }
  auto before = open->capture_snapshot();
  before.original_patch = patch;
  auto &history = context.services.histories[document];
  history.begin_transaction(
      "Reload from Disk", {},
      detail::step_from("Reload from Disk", {}, document, std::move(before)));
  session.accept_disk_version(document, patch, /*replace=*/true);
  if (history.commit_transaction(context)) {
    detail::sync_patch_url(context);
  }
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
