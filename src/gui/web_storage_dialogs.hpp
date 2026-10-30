#pragma once

#include "platform/platform_config.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

namespace ui {

void render_folder_import_ui();
void render_folder_delete_ui();
void render_awaited_flush_ui();

} // namespace ui

#endif
