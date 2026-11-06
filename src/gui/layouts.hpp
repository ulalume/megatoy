#pragma once

namespace ui::layouts {

/// Docks the default arrangement into the empty dockspace `dockspace_id`.
void build_default_layout(unsigned int dockspace_id);

/**
 * Puts the waveform above the keyboard's area while the window is too narrow
 * for the two side by side, and back beside it once there is room. Called
 * before the dockspace is submitted.
 */
void fit_footer_to_width();

/**
 * Narrows the patch browser before the patch editor as the window narrows,
 * down to its least width, then the editor to its own. Below the two least
 * widths together the browser is folded away. Called before the dockspace is
 * submitted.
 */
void fit_sidebar_to_width();

/// Whether the window is too narrow for the patch browser beside the editor,
/// as fit_sidebar_to_width() last judged.
bool patch_browser_folded();

/// The patch browser's least width beside the editor, and its width folded.
float patch_browser_min_width();

} // namespace ui::layouts
