#pragma once

#include "platform/platform_config.hpp"

#if !defined(MEGATOY_PLATFORM_WEB)

#include "preferences/preference_manager.hpp"

#include <filesystem>
#include <functional>

namespace patches {

/**
 * Bring `chosen`, a folder on disk, into the workspace, read in place.
 *
 * The folder has to pass PreferenceManager::check_chosen_workspace_folder(),
 * which is where `confirm` is used. It joins the workspace once a background
 * scan has warmed the parse cache for it, and `on_changed` runs then, from
 * background_folder_scan::poll_completion(). `preferences` has to live until
 * that scan is over.
 */
void add_chosen_workspace_folder(PreferenceManager &preferences,
                                 const std::filesystem::path &chosen,
                                 std::function<void()> on_changed,
                                 PreferenceManager::ConfirmReplace confirm);

} // namespace patches

#endif
