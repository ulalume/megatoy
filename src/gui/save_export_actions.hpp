#pragma once

#include "patches/patch_session.hpp"
#include "platform/platform_config.hpp"
#include "save_export_state.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace ui {

const char *save_label_for(const patches::PatchSession &session,
                           bool is_user_patch);

void trigger_save(patches::PatchSession &session, SaveExportState &state,
                  std::string_view extension_override = {});
void request_save_as(SaveExportState &state);
/// The `Download` submenu: every format a patch can be written in. Returns
/// the one picked, if any.
std::optional<std::string>
download_format_menu(const patches::PatchSession &session);
/// Ask for the browser storage dialog from outside the editor's window.
void request_save_to_storage(SaveExportState &state);

#if defined(MEGATOY_PLATFORM_WEB)
/// Serialize the editor's patch in one format and hand it to the browser.
void download_current_patch(patches::PatchSession &session,
                            const std::string &extension);
#endif

void render_save_export_popups(patches::PatchSession &session,
                               SaveExportState &state);

} // namespace ui
