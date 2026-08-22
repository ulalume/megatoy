#pragma once

#include "platform/platform_config.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include <functional>
#include <string>

namespace platform::web {

/**
 * Flush to browser storage and wait for it, behind a modal.
 *
 * For operations too big for the debounced fire-and-forget persist. Two
 * reasons to wait rather than to schedule:
 *
 * The flush snapshots the filesystem, then yields to read IndexedDB, then
 * reads the listed files one by one. Anything that moves a file during that
 * yield makes the second half read a path that is no longer there, which
 * comes back as ENOENT. Blocking input for the duration closes that window.
 *
 * And a caller that has to know whether the change reached IndexedDB, to put
 * a preference back if it did not, needs something to wait on.
 *
 * `on_complete` runs on a later frame, on the main thread. Returns false,
 * touching nothing, when a flush is already being awaited.
 */
bool begin_awaited_flush(std::string title, std::string detail,
                         std::function<void(bool ok, std::string error)>
                             on_complete);

/// Draw the modal shown while a flush is awaited. Once per frame.
void render_awaited_flush_ui();

} // namespace platform::web

#endif
