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

DialogResult save_file(const std::filesystem::path &default_dir,
                       const std::string &default_name,
                       const std::vector<FileFilter> &filters,
                       std::filesystem::path &selected_path);

} // namespace platform::file_dialog
