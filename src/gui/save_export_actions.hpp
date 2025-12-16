#pragma once

#include "patches/patch_session.hpp"
#include "save_export_state.hpp"
#include <string_view>

namespace ui {

bool is_patch_name_valid(const ym2612::Patch &patch);

const char *save_label_for(const patches::PatchSession &session,
                           bool is_user_patch);

void trigger_save(patches::PatchSession &session, SaveExportState &state,
                  bool force_overwrite,
                  std::string_view extension_override = {});

void trigger_export(patches::PatchSession &session, SaveExportState &state,
                    const patches::ExportFormatInfo &format);

void render_save_export_popups(patches::PatchSession &session,
                               SaveExportState &state);

// Duplicate helpers
void start_duplicate_dialog(patches::PatchSession &session,
                            SaveExportState &state);
void render_duplicate_dialog(patches::PatchSession &session,
                             SaveExportState &state);

} // namespace ui
