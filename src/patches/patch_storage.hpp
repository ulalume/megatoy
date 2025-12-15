#pragma once

#include "ym2612/patch.hpp"
#include <vector>

namespace patches {

struct PatchEntry;

// Extension point to plug in non-filesystem patch sources (e.g. web
// localStorage). Keeping it tiny avoids scattering platform ifdefs.
class PatchStorage {
public:
  virtual ~PatchStorage() = default;

  // Add top-level entries (e.g. a "localStorage" directory) into the repository
  // tree.
  virtual void append_entries(std::vector<PatchEntry> &tree) const = 0;

  // Attempt to load a patch referenced by the entry. Return true if handled.
  virtual bool load_patch(const PatchEntry &entry,
                          ym2612::Patch &out_patch) const = 0;

  // Attempt to remove the patch referenced by the entry. Default: unsupported.
  virtual bool remove_patch(const PatchEntry &) { return false; }
};

} // namespace patches
