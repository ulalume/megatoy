#pragma once

#include "platform/import_pipeline.hpp"
#include "platform/platform_config.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
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

enum class ImportPhase : int {
  AwaitingConfirmation = 0,
  Copying = 1,
  Validating = 2,
  Syncing = 3,
};

enum class SelectionPhase : int {
  WaitingForPicker = 0,
  Scanning = 1,
};

struct FolderSelectionProgress {
  SelectionPhase phase;
  bool is_drop;
  std::uint64_t scanned;
  std::uint64_t total;
};

struct FolderImportProgress {
  ImportPhase phase;
  std::string folder_name;
  std::size_t kept_count;
  std::uint64_t kept_bytes;
  std::size_t filtered_count;
  std::int64_t quota_remaining;
  std::size_t current_files;
  std::size_t total_files;
  std::uint64_t current_bytes;
  std::uint64_t total_bytes;
  bool cancel_requested;
};

/// Advance validation within the per-frame budget.
void advance_folder_import();
bool take_folder_import_finished();
bool take_folder_selection_finished();
std::optional<FolderSelectionProgress> folder_selection_progress();
void dismiss_folder_selection();
void cancel_folder_selection();
std::optional<FolderImportProgress> folder_import_progress();
void decide_folder_import(bool proceed);
void cancel_folder_import();

} // namespace platform::web

#endif
