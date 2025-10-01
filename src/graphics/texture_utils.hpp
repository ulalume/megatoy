#pragma once

#include <cstdint>
#include <imgui_impl_opengl3_loader.h>

namespace gfx {

bool create_texture_from_rgba(const uint8_t *pixels, int width, int height,
                              GLuint &texture_id);

} // namespace gfx
