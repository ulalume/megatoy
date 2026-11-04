#pragma once

#include "platform/platform_config.hpp"
#include <cstdint>
#if defined(MEGATOY_PLATFORM_WEB)
#include <GLES3/gl3.h>
#else
#include <imgui_impl_opengl3_loader.h>
#endif

namespace gfx {

/// Sampled nearest-neighbour unless `smooth`, which filters linearly for a
/// picture drawn at a size other than its own.
bool create_texture_from_rgba(const uint8_t *pixels, int width, int height,
                              GLuint &texture_id, bool smooth = false);

} // namespace gfx
