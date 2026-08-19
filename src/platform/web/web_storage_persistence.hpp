#pragma once

#include "platform/platform_config.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

namespace platform::web {

/// Flush browser filesystem mutations to IndexedDB after a short debounce.
void request_storage_persist();

} // namespace platform::web

#endif
