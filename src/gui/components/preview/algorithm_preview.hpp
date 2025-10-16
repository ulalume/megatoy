#pragma once

#include "preview.hpp"

namespace ui {

// Returns a cached texture for the given algorithm (0-7). Returns nullptr on
// failure.
const PreviewTexture *get_algorithm_preview_texture(int algorithm);

// Releases GPU resources created for algorithm previews.
void reset_algorithm_preview_textures();

} // namespace ui
