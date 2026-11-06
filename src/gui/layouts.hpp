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

} // namespace ui::layouts
