#pragma once

#include "patch_repository.hpp"
#include "system/directory_service.hpp"
#include "ym2612/patch.hpp"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <string>
#include <vector>

// Forward declaration
class AppState;

namespace patches {

struct SaveResult {
  enum class Status {
    Success,
    Cancelled,
    Error,
    Duplicated,
  };

  Status status;
  std::filesystem::path path;
  std::string error_message;

  static SaveResult success(const std::filesystem::path &exported_path) {
    return {Status::Success, exported_path, ""};
  }
  static SaveResult cancelled() { return {Status::Cancelled, {}, ""}; }
  static SaveResult error(const std::string &message) {
    return {Status::Error, {}, message};
  }
  static SaveResult duplicated() { return {Status::Duplicated, {}, ""}; }

  bool is_success() const { return status == Status::Success; }
  bool is_cancelled() const { return status == Status::Cancelled; }
  bool is_error() const { return status == Status::Error; }
  bool is_duplicated() const { return status == Status::Duplicated; }
};

struct PatchDropResult {
  enum class Status {
    Loaded,
    MultiInstrument,
    Error,
  };

  Status status = Status::Error;
  std::string history_label;
  ym2612::Patch patch;
  std::filesystem::path source_path;
  std::vector<ym2612::Patch> instruments; // For multi-instrument files
  std::string error_message;
};

// Check whether a character is valid in filenames
inline bool is_valid_filename_char(char c) {
  // Characters disallowed on Windows/Mac/Linux
  const char invalid_chars[] = {'<', '>', ':', '"', '/', '\\', '|', '?', '*'};
  // Control characters are also rejected
  if (c < 32 || c == 127) {
    return false;
  }
  // Check the invalid character list
  for (char invalid : invalid_chars) {
    if (c == invalid) {
      return false;
    }
  }
  return true;
}

// Normalise a filename by removing invalid characters
inline std::string sanitize_filename(const std::string &input) {
  std::string result;
  for (char c : input) {
    if (is_valid_filename_char(c)) {
      result += c;
    }
  }
  // Trim leading/trailing spaces and periods
  while (!result.empty() && (result.front() == ' ' || result.front() == '.')) {
    result.erase(0, 1);
  }
  while (!result.empty() && (result.back() == ' ' || result.back() == '.')) {
    result.pop_back();
  }
  return result;
}

enum class ExportFormat {
  DMP,
  MML,
};

class PatchManager {
public:
  explicit PatchManager(megatoy::system::DirectoryService &directories);

  ym2612::Patch &current_patch();
  const ym2612::Patch &current_patch() const;

  const std::string &current_patch_path() const;
  void set_current_patch_path(const std::filesystem::path &path);

  bool load_patch(const PatchEntry &entry);

  void refresh_directories();

  PatchRepository &repository();
  const PatchRepository &repository() const;

  SaveResult save_current_patch(bool force_overwrite);
  SaveResult export_current_patch_as(ExportFormat format);

  // File drop handling
  PatchDropResult load_patch_from_path(const std::filesystem::path &path);

  // Static callback for GLFW file drop
  static void handle_file_drop_callback(GLFWwindow *window, int count,
                                        const char **paths);

private:
  megatoy::system::DirectoryService &directories_;
  PatchRepository repository_;
  ym2612::Patch current_patch_;
  std::string current_patch_path_;
};

// Global function for file drop handling (for compatibility)
PatchDropResult load_patch_from_path(const std::filesystem::path &path);

// Setup file drop handling for a window
void setup_file_drop_handling(GLFWwindow *window, AppState *app_state);

} // namespace patches
