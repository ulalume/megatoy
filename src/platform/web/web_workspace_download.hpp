#pragma once

#include "platform/platform_config.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include <filesystem>

namespace platform {
class VirtualFileSystem;
}

namespace platform::web {

/// Download a workspace file directly, or a directory and its contents as ZIP.
bool download_workspace_path(const VirtualFileSystem &vfs,
                             const std::filesystem::path &path);

} // namespace platform::web

#endif
