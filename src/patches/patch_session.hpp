#pragma once

#include "channel_allocator.hpp"
#include "patch_repository.hpp"
#include "formats/patch_registry.hpp"
#include "patches/filename_utils.hpp"
#include "preferences/preference_manager.hpp"
#include "system/path_service.hpp"
#include "ym2612/note.hpp"
#include "ym2612/patch.hpp"
#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

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

using ExportFormatInfo = formats::ExportFormatInfo;

class PatchSession {
public:
  PatchSession(megatoy::system::PathService &directories,
               PreferenceManager &preferences, AudioManager &audio);

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

  // Initialization and workspace management
  void initialize_patch_defaults();
  /// Rebuild the repository if the workspace folder list has changed.
  void sync_workspace();

  // Patch loading
  void set_current_patch(const ym2612::Patch &patch,
                         const std::filesystem::path &source_path);

  // Audio integration
  void apply_patch_to_audio();

  // File operations
  /**
   * Write the patch back to its source file when that is safe, otherwise fall
   * back to Save As.
   *
   * In-place overwriting requires a single-patch, writable format in a
   * writable workspace folder. Read-only formats (.dmf, .fur, .opm,
   * .rym2612), instrument banks and the built-in presets all go through the
   * dialog instead, so nothing is destroyed by a stray Ctrl+S.
   */
  SaveResult save_current_patch(bool force_overwrite = false,
                                std::string_view preferred_extension = {});
  SaveResult save_current_patch_as(std::string_view preferred_extension = {});
  SaveResult export_current_patch_as(const ExportFormatInfo &format);
  std::optional<ExportFormatInfo>
  find_export_format(const std::string &extension) const;
  std::vector<ExportFormatInfo> export_formats() const;

  // Note management
  bool note_on(ym2612::Note note, uint8_t velocity,
               const PreferenceManager::UIPreferences &prefs);
  bool note_off(ym2612::Note note);
  bool note_is_active(const ym2612::Note &note) const;
  void release_all_notes();
  const std::array<bool, 6> &active_channels() const;
  const std::vector<ym2612::Note> active_notes() const;

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

  /**
   * True when Save writes straight back to the current file. False means the
   * next Save opens a dialog -- because the patch has no file yet, came from
   * a read-only folder, or is one instrument out of a bank.
   */
  bool can_save_in_place() const;

  bool current_patch_is_user_patch() const;
  const char *save_label_for(bool is_user_patch) const;

private:
  bool can_overwrite_in_place(const std::filesystem::path &path) const;
  std::optional<std::filesystem::path> writable_source_folder() const;
  bool write_patch_to(const std::filesystem::path &path);

  megatoy::system::PathService &directories_;
  PreferenceManager &preferences_;
  AudioManager &audio_;
  std::unique_ptr<PatchRepository> repository_;
  ChannelAllocator channel_allocator_;
  ym2612::Patch current_patch_;
  std::string current_patch_path_;
  ym2612::Patch original_patch_; // For tracking modifications
};

} // namespace patches
