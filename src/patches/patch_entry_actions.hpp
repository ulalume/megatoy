#pragma once

#include "patch_repository.hpp"

namespace patches {

class PatchSession;

struct EntryMenuCapabilities {
  bool create_patch = false;
  bool create_folder = false;
  bool save_current = false;
  bool download_folder = false;
  bool download_current = false;
  bool download_patch = false;
  bool save_to_storage = false;
  bool save_as = false;
  bool duplicate = false;
  bool rename = false;
  bool delete_patch = false;
  bool remove_folder = false;
  bool reveal = false;
};

struct EntryMenuActions {
  bool is_current = false;
  bool create_patch = false;
  bool create_folder = false;
  bool save_current = false;
  bool save_enabled = false;
  bool download_folder = false;
  bool download_patch = false;
  bool save_to_storage = false;
  bool save_as = false;
  bool duplicate = false;
  bool rate = false;
  bool reveal = false;
  bool rename = false;
  bool delete_patch = false;
  bool remove_folder = false;
};

bool entry_can_rename(const PatchRepository &repository,
                      const PatchEntry &entry, bool has_action,
                      bool protected_folder);
bool entry_can_delete(const PatchRepository &repository,
                      const PatchEntry &entry, bool has_action);
EntryMenuActions entry_menu_actions(const PatchSession &session,
                                    const PatchEntry &entry,
                                    const EntryMenuCapabilities &available,
                                    bool protected_folder,
                                    bool allow_remove_folder);

} // namespace patches
