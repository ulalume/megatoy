#pragma once

#include "app_state.hpp"
#include "gui/save_export_state.hpp"
#include "patches/patch_session.hpp"
#include <functional>
#include <imgui.h>
#include <string>

namespace ui {

using PatchEditorState = SaveExportState;

struct PatchEditorContext {
  patches::PatchSession &session;
  PreferenceManager::UIPreferences &prefs;
  UIState::EnvelopeState (&envelope_states)[4];
  OperatorEditState &operator_edit;
  UIState::TextPromptState &text_prompt_state;
  std::function<void(const std::string &label, const std::string &merge_key,
                     const ym2612::Patch &before)>
      begin_history;
  std::function<void()> commit_history;
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
 * frame.
 *
 * track_patch_history() is built for drags: it opens the step when the widget
 * activates and closes it when the widget deactivates, by which point the
 * value was written on an earlier frame. A checkbox writes on the frame it is
 * released -- the same frame the step closes -- so the "after" snapshot got
 * taken before the write, came out equal to "before", and the step was
 * dropped as a no-op. Handing the write in puts the two snapshots either side
 * of it.
 *
 * No merge key: two toggles of one checkbox are two separate steps.
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

// Function to render the instrument settings panel
void render_patch_editor(const char *title, PatchEditorContext &context,
                         PatchEditorState &state);

} // namespace ui
