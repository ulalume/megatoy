#pragma once

#include "app_state.hpp"

namespace ui {

/**
 * Unified patch UI component that consolidates:
 * - Patch editor (instrument settings panel)
 * - Patch selector (patch browser panel)
 * - Patch drop (file drop feedback)
 */

// Function to render the patch editor panel
void render_patch_editor(AppState &app_state);

// Function to render patch browser panel
void render_patch_selector(AppState &app_state);

// Function to render patch drop feedback
void render_patch_drop_feedback(AppState &app_state);

} // namespace ui
