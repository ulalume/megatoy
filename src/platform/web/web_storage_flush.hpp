#pragma once

#include "platform/platform_config.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include <functional>
#include <string>

namespace platform::web {

/**
 * Flush to browser storage and wait for it, behind a modal.
 *
 * The flush lists the filesystem, yields to read IndexedDB, then reads the
 * listed files; a file moved during that yield comes back as ENOENT, so
 * input stays blocked throughout. Callers that must know whether the change
 * reached IndexedDB get that from `on_complete`, which runs on a later
 * frame. Returns false, touching nothing, when a flush is already awaited.
 */
bool begin_awaited_flush(std::string title, std::string detail,
                         std::function<void(bool ok, std::string error)>
                             on_complete);

/// Draw the modal shown while a flush is awaited. Once per frame.
void render_awaited_flush_ui();

} // namespace platform::web

#endif
