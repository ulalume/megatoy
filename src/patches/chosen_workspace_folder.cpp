#include "patches/chosen_workspace_folder.hpp"

#include "core/status.hpp"
#include "patches/background_folder_scan.hpp"

#include <utility>

namespace patches {

void add_chosen_workspace_folder(PreferenceManager &preferences,
                                 const std::filesystem::path &chosen,
                                 std::function<void()> on_changed,
                                 PreferenceManager::ConfirmReplace confirm) {
  preferences.check_chosen_workspace_folder(
      chosen, std::move(confirm),
      [&preferences, on_changed = std::move(on_changed)](
          const std::filesystem::path &folder) {
        // Warm the parse cache on a worker before the folder joins the
        // workspace: the sync that follows re-walks it on the UI thread, and
        // for a folder of thousands of banks that first parse is a freeze
        // measured in seconds. `folder` is the path the workspace will store;
        // warmed under any other, the cache keys miss.
        const bool started = background_folder_scan::begin(
            folder, [&preferences, folder, on_changed](bool cancelled) {
              if (cancelled) {
                megatoy::status::info("Folder scan cancelled.");
                return;
              }
              if (preferences.add_workspace_folder(folder)) {
                if (on_changed) {
                  on_changed();
                }
              } else {
                megatoy::status::warning("\"" + folder.filename().string() +
                                         "\" is already in the workspace.");
              }
            });
        if (!started) {
          megatoy::status::warning("Another folder scan is in progress.");
        }
      });
}

} // namespace patches
