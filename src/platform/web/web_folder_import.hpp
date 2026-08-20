#pragma once

#include "platform/import_pipeline.hpp"
#include "platform/platform_config.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace platform::web {

struct FolderImportResult {
  bool ok = false;
  bool cancelled = false;
  /// Name of the folder the user chose, used as the workspace folder name.
  std::string folder_name;
  /// Where the files were written.
  std::filesystem::path path;
  std::size_t file_count = 0;
  std::size_t filtered_count = 0;
  std::vector<platform::import_pipeline::ValidationFailure> validation_failures;
  std::string error;
};

/**
 * Copy a folder of patches from the user's disk into the persistent
 * workspace.
 *
 * The browser cannot give a page ongoing access to a directory the way the
 * desktop file dialog does -- Firefox and Safari implement no directory
 * picker at all, and even where one exists every read is asynchronous, which
 * does not fit megatoy's synchronous filesystem interface. So this imports a
 * *copy*: the chosen tree is written under `destination_root` and from then
 * on behaves like any other workspace folder.
 *
 * Asynchronous, because the file picker and the reads are. `on_complete` runs
 * on a later frame; it is not called if the user cancels.
 */
void import_folder(const std::filesystem::path &destination_root,
                   std::function<void(FolderImportResult)> on_complete);

/**
 * Accept folders dragged onto the page.
 *
 * SDL's own drop handler covers plain files (it copies them into MEMFS and
 * raises SDL_EVENT_DROP_FILE), but it cannot read directories, so those drops
 * silently did nothing. This installs a capture-phase listener that takes
 * over only when a drop contains a directory: the tree is copied under
 * `destination_root` -- same as an Import Folder -- and `handler` is called
 * once per dropped directory. File-only drops still fall through to SDL.
 */
void set_drop_import_handler(std::function<void(FolderImportResult)> handler);
void install_drop_import(const std::filesystem::path &destination_root);

/// Draw the preflight, progress/cancel, and detailed result modals.
void render_folder_import_ui();

} // namespace platform::web

#endif
