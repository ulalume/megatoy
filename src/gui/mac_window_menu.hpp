#pragma once

namespace megatoy::gui {

// Takes Cmd+W off SDL's Window > Close menu item, so the key reaches the app
// and closes a panel or patch instead of the whole window. Call after SDL's
// video subsystem is initialized.
void release_close_window_shortcut();

} // namespace megatoy::gui
