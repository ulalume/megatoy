#include "patch_entry_actions.hpp"

#include "patch_session.hpp"

namespace patches {

bool entry_can_rename(const PatchRepository &repository,
                      const PatchEntry &entry, bool has_action,
                      bool protected_folder) {
  return has_action && !protected_folder && repository.can_rename_patch(entry);
}

bool entry_can_delete(const PatchRepository &repository,
                      const PatchEntry &entry, bool has_action) {
  return has_action && repository.can_delete_patch(entry);
}

EntryMenuActions entry_menu_actions(const PatchSession &session,
                                    const PatchEntry &entry,
                                    const EntryMenuCapabilities &available,
                                    bool protected_folder,
                                    bool allow_remove_folder) {
  const auto &repository = session.repository();
  EntryMenuActions actions;
  actions.is_current =
      !entry.is_directory &&
      entry.relative_path == session.current_patch_selection_path();
  const bool is_patch = !entry.is_directory;
  // A bank lists instruments like a folder, but cannot take new files.
  const bool writable_folder = entry.is_directory && entry.format.empty() &&
                               session.can_create_patch_in(entry.full_path);
  actions.create_patch = writable_folder && available.create_patch;
  actions.create_folder = writable_folder && available.create_folder;
  actions.save_current = actions.is_current &&
                         session.current_patch_is_user_patch() &&
                         available.save_current;
  actions.save_enabled = session.is_modified();
  actions.download_folder = entry.is_directory && available.download_folder;
  actions.download_patch =
      is_patch && (actions.is_current ? available.download_current
                                      : available.download_patch);
  actions.save_to_storage = actions.is_current && available.save_to_storage;
  actions.save_as =
      actions.is_current && !available.save_to_storage && available.save_as;
  actions.duplicate = is_patch && !actions.is_current && available.duplicate;
  actions.rate = is_patch && repository.can_edit_metadata(entry);
  actions.reveal = available.reveal;
  actions.rename =
      entry_can_rename(repository, entry, available.rename, protected_folder);
  actions.delete_patch =
      entry_can_delete(repository, entry, available.delete_patch);
  actions.remove_folder =
      allow_remove_folder && available.remove_folder && !protected_folder;
  return actions;
}

} // namespace patches
