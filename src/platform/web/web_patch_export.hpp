#pragma once

#include "platform/platform_config.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include <string>

namespace ym2612 {
struct Patch;
}

namespace platform::web {

/**
 * Hand a patch to the browser as a download.
 *
 * The web build has no save dialog, so exporting means producing a file the
 * browser offers to save. This lives here rather than behind the patch
 * storage interface because it is a property of the platform, not of wherever
 * the patch happens to be kept.
 *
 * @param extension_hint Target format, with or without a leading dot.
 */
bool export_patch(const ym2612::Patch &patch, const std::string &name,
                  const std::string &extension_hint);

} // namespace platform::web

#endif
