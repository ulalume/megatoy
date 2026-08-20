#pragma once

#include "platform/platform_config.hpp"

#if !defined(MEGATOY_PLATFORM_WEB)

namespace ui {

/**
 * Drive the background folder scan and show its progress modal.
 *
 * Called every frame: this is also what runs a finished scan's completion
 * callback on the UI thread. The modal only appears once a scan has run long
 * enough to be worth interrupting the user for.
 */
void render_folder_scan_dialog();

} // namespace ui

#endif
