#pragma once

#include "platform/platform_config.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include <filesystem>
#include <string_view>

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

/**
 * Serialize and download the current editor patch without saving it.
 *
 * `extension` names one of the registry's writable formats, dot included,
 * and picks the writer Save As would use for it. `filename_stem` names the
 * downloaded file; the patch's own name is used when it is empty.
 */
bool download_patch(const ym2612::Patch &patch, std::string_view extension,
                    std::string_view filename_stem);

} // namespace platform::web

#endif
