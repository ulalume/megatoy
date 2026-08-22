#pragma once

namespace ui {

/**
 * Ask for the change log to be shown. Deferred because the callers are not in
 * one ImGui id stack: the update toast has its own window, and a popup opened
 * from there would belong to it.
 */
void open_changelog_dialog();

/// Call once per frame, from the same scope every time.
void render_changelog_dialog();

} // namespace ui
