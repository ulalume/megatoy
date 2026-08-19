#pragma once

#include "platform/platform_config.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

namespace platform::web {

/// True when browser storage could not be populated at startup.
bool storage_load_failed();

/// Flush browser filesystem mutations to IndexedDB after a short debounce.
void request_storage_persist();

} // namespace platform::web

#endif
