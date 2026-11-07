#pragma once

namespace ui::layouts {

/**
 * Whether the window is too narrow for the patch browser beside the editor.
 * The browser then folds away, the waveform goes above the keyboard, and the
 * panels' tabs show only their icons.
 */
bool window_is_narrow();

/// Docks the default arrangement into the empty dockspace `dockspace_id`.
void build_default_layout(unsigned int dockspace_id);

/**
 * Puts the waveform above the keyboard's area while the window is too narrow
 * for the patch browser beside the editor, and back beside it once there is
 * room. Called before the dockspace is submitted.
 */
void fit_footer_to_width();

/**
 * While the waveform is above the keyboard, hands its height to the patch
 * editor when it is closed and takes it back when it is shown, so the
 * keyboard keeps its own. Called before the dockspace is submitted.
 */
void fit_footer_to_waveform();

/// The waveform's shown preference, for fit_footer_to_waveform to read.
void track_waveform_shown(const bool *shown);

/**
 * Narrows the patch browser before the patch editor as the window narrows,
 * down to its least width, then the editor to its own. Below the two least
 * widths together the browser is folded away. Called before the dockspace is
 * submitted.
 */
void fit_sidebar_to_width();

/// The patch browser's least width beside the editor, and its width folded.
float patch_browser_min_width();

} // namespace ui::layouts
