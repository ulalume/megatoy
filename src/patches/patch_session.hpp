#pragma once

#include "channel_allocator.hpp"
#include "formats/patch_registry.hpp"
#include "patch_document.hpp"
#include "patch_repository.hpp"
#include "patches/filename_utils.hpp"
#include "preferences/preference_manager.hpp"
#include "system/path_service.hpp"
#include "ym2612/note.hpp"
#include "ym2612/patch.hpp"
#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Forward declaration
class AudioManager;

namespace patches {

class PersistentParseCache;

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

using SaveFormatInfo = formats::SaveFormatInfo;

class PatchSession {
public:
  enum class RememberPatchPath { Yes, No };

  PatchSession(megatoy::system::PathService &directories,
               PreferenceManager &preferences, AudioManager &audio,
               PersistentParseCache *persistent_cache = nullptr);

  using PatchSnapshot = PatchDocument::Snapshot;

  /**
   * The open documents, in the order they were opened. There is always at
   * least one. The active one is what plays, and what every call below that
   * does not name a document acts on.
   */
  const std::vector<std::unique_ptr<PatchDocument>> &documents() const;
  PatchDocument &active_document();
  const PatchDocument &active_document() const;
  DocumentId active_document_id() const;
  PatchDocument *find_document(DocumentId id);
  const PatchDocument *find_document(DocumentId id) const;
  /// The browser's entry for a repository-relative path: the entry itself,
  /// or failing that the first one read from that file.
  const PatchEntry *find_entry(const std::string &relative_path) const;
  /// The open document showing this repository entry, if any.
  std::optional<DocumentId>
  find_document_by_selection_path(const std::filesystem::path &path) const;
  /// False when no document has that id.
  bool activate(DocumentId id);
  /**
   * Open `patch` as a new document, as set_current_patch() would load it,
   * without activating it. `selection_path` overrides the entry the source
   * path selects, as for a version inside a package.
   */
  DocumentId open_document(const ym2612::Patch &patch,
                           const std::filesystem::path &source_path,
                           const std::filesystem::path &selection_path = {},
                           bool pinned = true);
  /// A new document holding default_patch(), as a fresh launch opens it.
  DocumentId open_default_document(bool pinned = true);
  /**
   * An untitled document holding default_patch(), activated and kept open.
   * Like create_patch_in(), it takes over an unedited preview.
   */
  DocumentId open_new_document();
  /// A new document loaded from `entry`; empty when it cannot be read.
  std::optional<DocumentId> open_entry(const PatchEntry &entry,
                                       bool pinned = true);
  /**
   * Show `entry` and make its document the active one. A document already
   * showing it is activated. Otherwise the preview document takes it when it
   * has no unsaved changes; a preview with changes is pinned and a new
   * document opens instead. `pinned` keeps the result open when the next one
   * is opened. Without `reuse_preview` a new document opens and the preview
   * stays. Empty when the entry cannot be read.
   */
  std::optional<DocumentId> open_or_activate(const PatchEntry &entry,
                                             bool pinned,
                                             bool reuse_preview = true);
  /**
   * The same for a patch read from `source_path` outside the browser, such
   * as a dropped file. A document opened from that file that still holds the
   * same sound is activated instead.
   */
  DocumentId open_or_activate_patch(const ym2612::Patch &patch,
                                    const std::filesystem::path &source_path,
                                    bool pinned);
  /// open_or_activate() for the repository entry at `path`; empty when the
  /// browser does not list it.
  std::optional<DocumentId> open_path(const std::filesystem::path &path,
                                      bool pinned);
  /// The document open as a preview, if any. There is at most one.
  std::optional<DocumentId> preview_document_id() const;
  /// Keep a preview document open; false when no document has that id.
  bool pin(DocumentId id);
  /// An open document that plays like `patch`, the active one first.
  std::optional<DocumentId>
  find_document_with_sound(const ym2612::Patch &patch) const;
  /// What an open document's file holds after it was written outside
  /// megatoy.
  struct DiskVersion {
    DocumentId document = 0;
    ym2612::Patch patch;
  };
  /**
   * The open documents whose file has been written since megatoy last read
   * or saved it, and now holds something other than the document's saved
   * contents. Each write is reported once.
   */
  std::vector<DiskVersion> take_disk_versions();
  /**
   * Whether a file open from outside the workspace has been written since
   * take_disk_versions() last looked. Those files have no folder being
   * watched, so this asks each of them.
   */
  bool outside_file_changed() const;
  /// Take `patch` as what the document's file holds, and show it in place of
  /// the contents when `replace`.
  void accept_disk_version(DocumentId id, const ym2612::Patch &patch,
                           bool replace);
  /**
   * Rate an entry's patch, 0 for none, or file it under a category, in its
   * folder's metadata. False when the entry cannot carry metadata or the
   * write failed.
   */
  bool set_entry_rating(const PatchEntry &entry, int stars);
  bool set_entry_category(const PatchEntry &entry, const std::string &category);
  /// Pin every preview document that has been edited.
  void pin_modified_documents();
  /**
   * Close a document. Closing the active one activates its neighbour, the
   * next one when there is one. Closing the last one leaves a fresh default
   * document in its place, as a preview.
   */
  bool close_document(DocumentId id);
  bool any_document_modified() const;
  std::size_t modified_document_count() const;
  void mark_all_clean();

  // Patch access
  ym2612::Patch &current_patch();
  const ym2612::Patch &current_patch() const;

  // Modification tracking
  bool is_modified() const;
  void mark_as_clean();

  // The source path is the file Save targets. The selection path identifies
  // one visible repository entry, which can be a version inside that file.
  const std::string &current_patch_path() const;
  void
  set_current_patch_path(const std::filesystem::path &path,
                         RememberPatchPath remember = RememberPatchPath::Yes);
  const std::string &current_patch_selection_path() const;
  void set_current_patch_selection_path(const std::filesystem::path &path);

  // Repository access
  PatchRepository &repository();
  const PatchRepository &repository() const;

  /**
   * Remember the open documents that have a file, with the active one, so
   * the next launch can open them again. Unsaved changes are not kept.
   */
  void remember_open_documents();
  /**
   * Reopen what remember_open_documents() last kept, in order, skipping
   * files that are gone, and activate the one that was active. The document
   * the session started with is closed once any of them opened. False when
   * nothing was reopened.
   */
  bool restore_open_documents();

  // Initialization and workspace management
  void initialize_patch_defaults();
  /**
   * The patch a new one starts from: the built-in presets' init.dmp when it
   * is there, otherwise megatoy's own defaults. The same patch a fresh launch
   * opens.
   */
  ym2612::Patch default_patch() const;
  /// Rebuild the repository if the workspace folder list has changed.
  void sync_workspace();

  // Patch loading
  bool load_patch_from_entry(const PatchEntry &entry);
  bool restore_patch(const std::filesystem::path &relative_path);
  void set_current_patch(const ym2612::Patch &patch,
                         const std::filesystem::path &source_path,
                         RememberPatchPath remember = RememberPatchPath::Yes);

  // Audio integration
  void apply_patch_to_audio();
  bool apply_patch_to_audio_if_changed();

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
  SaveResult save_current_patch(std::string_view preferred_extension = {});
  SaveResult save_current_patch_as(std::string_view preferred_extension = {},
                                   std::string_view filename_stem = {});
  SaveResult
  save_current_patch_as_forced(std::string_view preferred_extension = {},
                               std::string_view filename_stem = {});
  /// Save As into one named workspace folder rather than the default one.
  SaveResult save_current_patch_as_in(const std::filesystem::path &folder,
                                      std::string_view preferred_extension = {},
                                      std::string_view filename_stem = {});
  SaveResult
  save_current_patch_as_in_forced(const std::filesystem::path &folder,
                                  std::string_view preferred_extension = {},
                                  std::string_view filename_stem = {});
  /// True when a new patch may be written straight into `folder`.
  bool can_create_patch_in(const std::filesystem::path &folder) const;
  /**
   * Write default_patch() into `folder` as `stem` + `extension`, then open it.
   *
   * On success the repository has been refreshed and the new file is the
   * current patch and the browser's selection, exactly as if the user had
   * clicked it there.
   */
  SaveResult create_patch_in(const std::filesystem::path &folder,
                             const std::string &stem,
                             std::string_view extension);
  /**
   * Create the folder `name` inside `parent`, under the same conditions as
   * create_patch_in(), and refresh the repository. The editor is left as it
   * is.
   */
  SaveResult create_folder_in(const std::filesystem::path &parent,
                              const std::string &name);
  bool rename_patch(const PatchEntry &entry, const std::string &new_stem);
  /**
   * Delete `entry` from the repository. Every document showing it, or
   * something inside it, loses its path and becomes an unsaved patch.
   */
  bool delete_patch(const PatchEntry &entry);
  /**
   * Move `entry` into the folder `destination`, within one workspace folder.
   * Every open document follows when it was the entry or inside it, as does
   * a workspace folder registered at the moved path. Renaming does the same.
   */
  bool can_move_patch(const PatchEntry &entry,
                      const PatchEntry &destination) const;
  bool move_patch(const PatchEntry &entry, const PatchEntry &destination);
  std::optional<SaveFormatInfo>
  find_save_format(const std::string &extension) const;
  std::vector<SaveFormatInfo> save_formats() const;

  // Note management
  bool note_on(ym2612::Note note, uint8_t velocity,
               const PreferenceManager::UIPreferences &prefs);
  bool note_off(ym2612::Note note);
  bool note_is_active(const ym2612::Note &note) const;
  void release_all_notes();
  std::array<bool, 6> active_channels() const;
  const std::vector<ym2612::Note> active_notes() const;
  /// What each channel is playing -- or was, until its key came up -- with
  /// the chip's clock, so the envelope graph can place a cursor on it.
  VoiceActivityFrame voice_activity() const;

  // Snapshot functionality for undo/redo
  PatchSnapshot capture_snapshot() const;
  void restore_snapshot(const PatchSnapshot &snapshot);
  /// Restore into one document; nothing happens once it has been closed.
  void restore_snapshot(DocumentId id, const PatchSnapshot &snapshot);

  /**
   * True when Save writes straight back to the current file. False means the
   * next Save opens a dialog -- because the patch has no file yet, came from
   * a read-only folder, or is one instrument out of a bank.
   */
  bool can_save_in_place() const;
  bool can_save_in_place(const PatchDocument &document) const;

  bool current_patch_is_user_patch() const;
  bool is_user_patch(const PatchDocument &document) const;
  const char *save_label_for(bool is_user_patch) const;
  const char *save_label_for(const PatchDocument &document,
                             bool is_user_patch) const;

  /// The folder holding the current patch's file, when it may be written to:
  /// in the workspace or outside it. Empty for a new patch, the built-in
  /// presets, or a read-only workspace folder.
  std::optional<std::filesystem::path> writable_source_folder() const;
  std::optional<std::filesystem::path>
  writable_source_folder(const PatchDocument &document) const;

private:
  /// default_patch() together with the file it came from, empty when the
  /// built-in defaults were used.
  struct DefaultPatch {
    ym2612::Patch patch;
    std::filesystem::path source;
  };
  DefaultPatch default_patch_source() const;

  bool is_active(const PatchDocument &document) const;
  PatchDocument &create_document(bool pinned);
  /// What set_current_patch() does to the document, short of audio.
  void assign_patch(PatchDocument &document, const ym2612::Patch &patch,
                    const std::filesystem::path &source_path,
                    RememberPatchPath remember);
  void assign_default_patch(PatchDocument &document);
  /// Note when the document's file was last written, as megatoy read or
  /// saved it.
  void remember_disk_time(const PatchDocument &document);
  /**
   * Fold every workspace folder that lies inside another into the outer one:
   * its ratings and categories move to the outer folder's sidecar, it leaves
   * the list with the outer one taking its place, and the documents, the
   * remembered ones and the last patch are re-pointed at the outer folder's
   * paths. My Patches is never let go of.
   */
  void absorb_nested_folders();
  /// The entry's metadata with `change` applied, written and the tree
  /// rebuilt to show it.
  template <typename Change>
  bool update_entry_metadata(const PatchEntry &entry, Change &&change);
  /// The preference only follows the active document.
  void set_document_path(PatchDocument &document,
                         const std::filesystem::path &path,
                         RememberPatchPath remember = RememberPatchPath::Yes);
  void set_document_selection_path(PatchDocument &document,
                                   const std::filesystem::path &path);
  std::string selection_key(const std::filesystem::path &path) const;
  /**
   * Where a repository-relative path points on disk, written the same way
   * every time, so two paths naming one file compare equal whatever folder
   * they reach it through. One no folder resolves stays as it is.
   */
  std::string file_identity(const std::string &relative_path) const;
  /**
   * Put `patch` in the preview document, or in a new one when there is no
   * preview, it has unsaved changes or `reuse_preview` is false, and
   * activate it.
   */
  DocumentId place_document(const ym2612::Patch &patch,
                            const std::filesystem::path &source_path,
                            const std::filesystem::path &selection_path,
                            bool pinned, bool reuse_preview = true);

  /// `folder` names one workspace folder to write into; empty means the
  /// repository's own default target.
  SaveResult save_current_patch_as_impl(std::string_view preferred_extension,
                                        std::string_view filename_stem,
                                        bool overwrite,
                                        const std::filesystem::path &folder);

  /// What has to follow an entry when it changes path, captured before it
  /// does: every relative path under it is about to change.
  struct Relocation {
    std::filesystem::path from;
    std::filesystem::path to;
    std::optional<std::filesystem::path> workspace_folder;
    struct Follower {
      DocumentId id;
      std::filesystem::path absolute;
      std::string selection_suffix;
    };
    std::vector<Follower> documents;
  };
  /// Empty when the entry is a workspace folder that may not be relocated.
  std::optional<Relocation> prepare_relocation(const PatchEntry &entry,
                                               std::filesystem::path to) const;
  void follow_relocation(const Relocation &relocation, bool renamed);

  megatoy::system::PathService &directories_;
  PreferenceManager &preferences_;
  AudioManager &audio_;
  std::unique_ptr<PatchRepository> repository_;
  std::vector<std::unique_ptr<PatchDocument>> documents_;
  std::map<DocumentId, std::filesystem::file_time_type> disk_times_;
  DocumentId active_id_ = 0;
  DocumentId next_id_ = 1;
  /// What the chip has, whichever document it came from.
  ym2612::Patch last_applied_;
  bool has_applied_patch_ = false;
};

} // namespace patches
