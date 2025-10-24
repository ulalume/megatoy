#pragma once

#include "channel_allocator.hpp"
#include "patch_repository.hpp"
#include "preferences/preference_manager.hpp"
#include "system/path_service.hpp"
#include "ym2612/note.hpp"
#include "ym2612/patch.hpp"
#include <GLFW/glfw3.h>
#include <array>
#include <filesystem>
#include <string>

// Forward declaration
class AudioManager;

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

enum class ExportFormat {
  DMP,
  MML,
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

class PatchSession {
public:
  PatchSession(megatoy::system::PathService &directories, AudioManager &audio);

  // Patch access
  ym2612::Patch &current_patch();
  const ym2612::Patch &current_patch() const;

  // Modification tracking
  bool is_modified() const;
  void mark_as_clean();

  // Path management
  const std::string &current_patch_path() const;
  void set_current_patch_path(const std::filesystem::path &path);

  // Repository access
  PatchRepository &repository();
  const PatchRepository &repository() const;

  // Initialization and directory management
  void initialize_patch_defaults();
  void refresh_directories();

  // Patch loading
  void set_current_patch(const ym2612::Patch &patch,
                         const std::filesystem::path &source_path);

  // Audio integration
  void apply_patch_to_audio();

  // File operations
  SaveResult save_current_patch(bool force_overwrite = false);
  SaveResult export_current_patch_as(ExportFormat format);

  // Note management
  bool note_on(ym2612::Note note, uint8_t velocity,
               const PreferenceManager::UIPreferences &prefs);
  bool note_off(ym2612::Note note);
  bool note_is_active(const ym2612::Note &note) const;
  void release_all_notes();
  const std::array<bool, 6> &active_channels() const;

  // Snapshot functionality for undo/redo
  struct PatchSnapshot {
    ym2612::Patch original_patch;
    ym2612::Patch patch;
    std::string path;
    bool operator==(const PatchSnapshot &other) const {
      return patch == other.patch && path == other.path;
    }
  };

  PatchSnapshot capture_snapshot() const;
  void restore_snapshot(const PatchSnapshot &snapshot);

  bool current_patch_is_user_patch() const;

private:
  megatoy::system::PathService &directories_;
  AudioManager &audio_;
  PatchRepository repository_;
  ChannelAllocator channel_allocator_;
  ym2612::Patch current_patch_;
  std::string current_patch_path_;
  ym2612::Patch original_patch_; // For tracking modifications
};

} // namespace patches
