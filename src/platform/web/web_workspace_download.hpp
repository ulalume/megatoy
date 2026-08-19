#pragma once

#include "platform/platform_config.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include <filesystem>

namespace platform {
class VirtualFileSystem;
}

namespace ym2612 {
struct Patch;
}

namespace platform::web {

/// Download a workspace file directly, or a directory and its contents as ZIP.
bool download_workspace_path(const VirtualFileSystem &vfs,
                             const std::filesystem::path &path);

/// Serialize and download the current editor patch without saving it.
bool download_patch(const ym2612::Patch &patch);

} // namespace platform::web

#endif
