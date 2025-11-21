#pragma once

#include "patches/patch_session.hpp"
#include "save_export_state.hpp"

namespace ui {

bool is_patch_name_valid(const ym2612::Patch &patch);

const char *save_label_for(const patches::PatchSession &session,
                           bool is_user_patch);

void trigger_save(patches::PatchSession &session, SaveExportState &state,
                  bool force_overwrite);

void trigger_export(patches::PatchSession &session, SaveExportState &state,
                    patches::ExportFormat format);

void render_save_export_popups(patches::PatchSession &session,
                               SaveExportState &state);

} // namespace ui
