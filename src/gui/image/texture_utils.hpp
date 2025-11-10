#pragma once

#include <cstdint>
#include "platform/platform_config.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include <GLES3/gl3.h>
#else
#include <imgui_impl_opengl3_loader.h>
#endif

namespace gfx {

bool create_texture_from_rgba(const uint8_t *pixels, int width, int height,
                              GLuint &texture_id);

} // namespace gfx
