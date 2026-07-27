#pragma once

#include "platform/platform_config.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include <filesystem>

namespace megatoy::workspace {
class Workspace;
}

namespace platform::web {

/**
 * Prepare the browser's persistent workspace on startup.
 *
 * Creates a writable home folder under the IDBFS mount so a fresh visitor can
 * save straight away, and moves any library left over from the old
 * single-blob localStorage store into it.
 *
 * Returns true when the workspace changed and preferences should be saved.
 */
bool bootstrap_workspace(megatoy::workspace::Workspace &workspace,
                         const std::filesystem::path &storage_root);

/// Name of the folder created for patches the user saves in the browser.
inline constexpr const char *kDefaultFolderName = "My Patches";

} // namespace platform::web

#endif
