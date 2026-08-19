#pragma once

#include "app_state.hpp"
#include "patches/patch_session.hpp"
#include "save_export_state.hpp"
#include <string_view>

namespace ui {

const char *save_label_for(const patches::PatchSession &session,
                           bool is_user_patch);

void trigger_save(patches::PatchSession &session, SaveExportState &state,
                  std::string_view extension_override = {});
void request_save_as(SaveExportState &state);

void render_save_export_popups(patches::PatchSession &session,
                               SaveExportState &state,
                               UIState::TextPromptState &text_prompt_state);

} // namespace ui
