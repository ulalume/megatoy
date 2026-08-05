#pragma once

namespace ui {

/**
 * Draw the notification toasts in the bottom-right corner.
 *
 * Info and Success fade out on their own after a few seconds; Warning and
 * Error stay until clicked, since a message about a failed save must not
 * disappear while the user is looking away. Call once per frame, after the
 * windows, so toasts draw on top.
 */
void render_status_toasts();

} // namespace ui
