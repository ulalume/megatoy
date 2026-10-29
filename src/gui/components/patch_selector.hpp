#pragma once

#include "patches/patch_repository.hpp"
#include "patches/patch_session.hpp"
#include "preferences/preference_manager.hpp"
#include <filesystem>
#include <functional>
#include <imgui.h>
#include <optional>
#include <string>

namespace ui {

enum class PendingMenuAction { SaveCurrent };

struct PendingRating {
  std::string relative_path;
  int stars = 0;
};

struct PendingMove {
  patches::PatchEntry entry;
  patches::PatchEntry destination;
};

/// Drag payload of a browser entry: its repository-relative path.
inline constexpr const char *kPatchEntryPayload = "MEGATOY_PATCH_ENTRY";

struct PatchSelectorContext {
  patches::PatchRepository &repository;
  patches::PatchSession &session;
  PreferenceManager::UIPreferences &prefs;
  /// Open an entry in the editor; `pinned` keeps it open rather than
  /// previewing it.
  std::function<void(const patches::PatchEntry &, bool pinned)> open_patch;
  std::function<void(const std::filesystem::path &)> reveal_in_file_manager;
  std::function<void(const patches::PatchEntry &)> download_entry;
  /// Download one entry's patch in the named format. Absent where there is
  /// nowhere to download to.
  std::function<void(const patches::PatchEntry &, const std::string &)>
      download_patch_entry;
  /// The same for the editor's patch, which may differ from its file.
  std::function<void(const std::string &)> download_current_patch;
  std::function<void()> save_current_patch;
  std::function<void()> save_current_patch_as;
  /// Where Save As has no file dialog to open: the browser's own dialog.
  std::function<void()> save_current_patch_to_storage;
  /// True when the workspace has no folders, so the browser can offer to add
  /// one instead of showing an empty tree.
  bool workspace_is_empty = false;
  std::function<void()> add_folder;
  std::function<bool(const std::filesystem::path &)> folder_is_protected;
  std::function<void(const std::filesystem::path &)> remove_folder;
  std::function<void(const patches::PatchEntry &)> rename_patch;
  std::function<void(const patches::PatchEntry &)> delete_patch;
  /// Move an entry into a folder.
  std::function<void(const patches::PatchEntry &, const patches::PatchEntry &)>
      move_patch;
  /// Copy the entry's patch to a new file, leaving the editor alone.
  std::function<void(const patches::PatchEntry &)> duplicate_patch;
  /// Ask for a new patch in this folder. Only offered on a folder the
  /// session says it can write into.
  std::function<void(const std::filesystem::path &)> create_patch_in;
  /// Ask for a new folder in this folder. Offered where create_patch_in is.
  std::function<void(const std::filesystem::path &)> create_folder_in;
  std::optional<std::filesystem::path> pending_remove_folder;
  std::optional<PendingMenuAction> pending_menu_action;
  std::optional<PendingRating> pending_rating;
  /// A drop in the tree, carried out once the tree is no longer being drawn.
  std::optional<PendingMove> pending_move;
};

// Function to render patch browser panel
void render_patch_selector(const char *title, PatchSelectorContext &context);

} // namespace ui
