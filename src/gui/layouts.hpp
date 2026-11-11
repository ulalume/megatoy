#pragma once

namespace ui::layouts {

/**
 * Whether the window is too narrow for the library beside the editor.
 * The browser then folds away, the waveform goes above the keyboard, and the
 * panels' tabs show only their icons.
 */
bool window_is_narrow();

/// Gives a view the settings saved under its former name, so a saved
/// arrangement keeps it where it was. Called once the settings are loaded.
void adopt_former_names();

/// Docks the default arrangement into the empty dockspace `dockspace_id`.
void build_default_layout(unsigned int dockspace_id);

/**
 * Puts the waveform above the keyboard's area while the window is too narrow
 * for the library beside the editor, and back beside it once there is
 * room. Called before the dockspace is submitted.
 */
void fit_footer_to_width();

/**
 * While the waveform is above the keyboard, hands its height to the patch
 * editor when it is closed and takes it back when it is shown, so the
 * keyboard keeps its own. `waveform_shown` is its preference. Called before
 * the dockspace is submitted.
 */
void fit_footer_to_waveform(bool waveform_shown);

/**
 * Narrows the library before the patch editor as the window narrows,
 * down to its least width, then the editor to its own. Below the two least
 * widths together the browser is folded away. Called before the dockspace is
 * submitted.
 */
void fit_sidebar_to_width();

/// The library's least width beside the editor, and its width folded.
float library_min_width();

} // namespace ui::layouts
