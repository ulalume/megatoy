#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace platform::file_dialog {

struct FileFilter {
  std::string label;                   // Optional descriptive label
  std::vector<std::string> extensions; // e.g. {"txt", "mml"}
};

enum class DialogResult {
  Ok,
  Cancelled,
  Error,
};

bool initialize();
void shutdown();

DialogResult pick_folder(const std::filesystem::path &default_path,
                         std::filesystem::path &selected_path);

/**
 * Asks where to save `stem`, and in which of `filters`' formats where there
 * are several: the dialog starts on the first, and the name's extension
 * follows the one picked. Which was picked is told by the extension of
 * `selected_path`.
 */
DialogResult save_file(const std::filesystem::path &default_dir,
                       const std::string &stem,
                       const std::vector<FileFilter> &filters,
                       std::filesystem::path &selected_path);

/// `stem` with the first of `filters`' extensions, as a save dialog starts.
std::string name_with_first_extension(const std::string &stem,
                                      const std::vector<FileFilter> &filters);

} // namespace platform::file_dialog
