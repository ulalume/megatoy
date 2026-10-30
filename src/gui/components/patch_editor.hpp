#pragma once

#include "app_state.hpp"
#include "gui/save_export_state.hpp"
#include "patches/patch_session.hpp"
#include <functional>
#include <imgui.h>
#include <optional>
#include <string>

namespace ui {

using PatchEditorState = SaveExportState;

struct PatchEditorContext {
  patches::PatchSession &session;
  AudioManager &audio;
  PreferenceManager::UIPreferences &prefs;
  /// One document's editor state.
  std::function<UIState::DocumentUi &(patches::DocumentId)> document_ui;
  ym2612::OperatorClipboard &operator_clipboard;
  /// Opens an undo step on the document being drawn, making it the active
  /// one first.
  std::function<void(const std::string &label, const std::string &merge_key,
                     const ym2612::Patch &before)>
      begin_history;
  std::function<void()> commit_history;
  /// Make a document the active one.
  std::function<void(patches::DocumentId)> activate;
  /// Close a document, asking first when it has unsaved changes.
  std::function<void(patches::DocumentId)> request_close;
  std::optional<patches::DocumentId> &reveal_document;
  unsigned int &editor_dock_id;
  /// The document whose window is being drawn.
  patches::DocumentId document = 0;
  /// Show a document's file's new contents, as one undo step.
  std::function<void(patches::DocumentId, const ym2612::Patch &)>
      reload_from_disk;

  OperatorEditState &operator_edit() const {
    return document_ui(document).operator_edit;
  }
  UIState::EnvelopeState &envelope_state(int slot) const {
    return document_ui(document).envelope_states[slot];
  }
};

void track_patch_history(PatchEditorContext &context, const std::string &label,
                         const std::string &merge_key = {});
inline void track_patch_history(PatchEditorContext &context, const char *label,
                                const char *merge_key = nullptr) {
  track_patch_history(context, std::string(label),
                      (merge_key && merge_key[0]) ? std::string(merge_key)
                                                  : std::string(label));
}

/**
 * Record one undo step around a change that begins and ends in the same
 * frame, taking the two snapshots either side of the write.
 *
 * track_patch_history() opens on activation and closes on deactivation,
 * which fits a drag but not a checkbox: that writes on the frame it is
 * released, so the closing snapshot would come before the write. No merge
 * key here, so two toggles are two steps.
 */
template <typename Apply>
void track_instant_patch_history(PatchEditorContext &context,
                                 const std::string &label, Apply &&apply) {
  if (context.begin_history) {
    context.begin_history(label, {}, context.session.current_patch());
  }
  apply();
  if (context.commit_history) {
    context.commit_history();
  }
}

/// One editor window per open document, docked together as tabs.
void render_patch_editors(PatchEditorContext &context, PatchEditorState &state);

/// True while one of the editor windows has the keyboard.
bool patch_editor_has_focus();

} // namespace ui
