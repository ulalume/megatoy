#pragma once

namespace ui {

/**
 * Ask for the change log to be shown.
 *
 * Deferred rather than opening the popup on the spot, because the callers are
 * not all in the same ImGui id stack -- the update toast is drawn in its own
 * window, and a popup opened from there would be a popup of that window.
 */
void open_changelog_dialog();

/// Call once per frame, from the same scope every time.
void render_changelog_dialog();

} // namespace ui
