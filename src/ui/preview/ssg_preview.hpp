#pragma once

#include "preview.hpp"

namespace ui {

// Returns a cached texture for the given SSG envelope (0-7).
const PreviewTexture *get_ssg_preview_texture(int envelope_type);

// Returns preview texture for the "off" (disabled) state, or nullptr.
const PreviewTexture *get_ssg_preview_off_texture();

// Releases GPU resources created for SSG previews.
void reset_ssg_preview_textures();

} // namespace ui
