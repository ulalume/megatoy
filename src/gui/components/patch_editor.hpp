#pragma once

#include "app_state.hpp"
#include "patches/patch_session.hpp"
#include <imgui.h>

#include <functional>
#include <string>

namespace ui {

struct PatchEditorState {
  std::string last_export_path;
  std::string last_export_error;
};

struct PatchEditorContext {
  patches::PatchSession &session;
  PreferenceManager::UIPreferences &prefs;
  UIState::EnvelopeState (&envelope_states)[4];
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

// Function to render the instrument settings panel
void render_patch_editor(const char *title, PatchEditorContext &context,
                         PatchEditorState &state);

} // namespace ui
