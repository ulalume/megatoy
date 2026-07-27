#pragma once

#include "platform/platform_config.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include <filesystem>
#include <functional>
#include <string>

namespace platform::web {

struct FolderImportResult {
  bool ok = false;
  /// Name of the folder the user chose, used as the workspace folder name.
  std::string folder_name;
  /// Where the files were written.
  std::filesystem::path path;
  std::size_t file_count = 0;
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

} // namespace platform::web

#endif
