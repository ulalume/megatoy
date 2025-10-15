#pragma once

#include "../system/directory_service.hpp"
#include "../ym2612/patch.hpp"
#include "patch_repository.hpp"

#include <filesystem>
#include <string>

namespace patches {

struct ExportResult {
  enum class Status {
    Success,
    Cancel,
    Error,
  };

  Status status;
  std::filesystem::path path;
  std::string error_message;

  static ExportResult success(const std::filesystem::path &exported_path) {
    return {Status::Success, exported_path, ""};
  }
  static ExportResult cancelled() { return {Status::Cancel, {}, ""}; }
  static ExportResult error(const std::string &message) {
    return {Status::Error, {}, message};
  }
  bool is_success() const { return status == Status::Success; }
  bool is_cancelled() const { return status == Status::Cancel; }
  bool is_error() const { return status == Status::Error; }
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

  bool save_current_patch();
  ExportResult export_current_patch_as(ExportFormat format);

private:
  megatoy::system::DirectoryService &directories_;
  PatchRepository repository_;
  ym2612::Patch current_patch_;
  std::string current_patch_path_;
};

} // namespace patches
