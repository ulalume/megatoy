#pragma once

// The native dialogs that ask the user for a path.

#include "patches/patch_write.hpp"
#include "preferences/preference_manager.hpp"

#include <functional>

namespace ui {

/// Asks where to save a patch and in which format.
patches::SavePathAnswer ask_save_path(const patches::SavePathRequest &request);

/**
 * Bring a folder of patches into the workspace.
 *
 * On desktop this opens the native folder picker and the folder is read in
 * place. The browser cannot reference a folder on disk that way, so there it
 * imports a copy into persistent storage instead, and `on_changed` may run
 * several frames later. It is not called if the user cancels.
 */
void request_add_workspace_folder(PreferenceManager &preferences,
                                  std::function<void()> on_changed,
                                  PreferenceManager::ConfirmReplace confirm);

} // namespace ui
