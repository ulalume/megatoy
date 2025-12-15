#pragma once

#include "patch_metadata.hpp"
#include "ym2612/patch.hpp"
#include <filesystem>
#include <optional>
#include <vector>

namespace patches {

struct PatchEntry;
struct PatchMetadata;

struct SavePatchResult {
  enum class Status {
    Success,
    Duplicate,
    Error,
    Unsupported,
  };

  Status status = Status::Unsupported;
  std::filesystem::path path;
  std::string error_message;

  static SavePatchResult success(const std::filesystem::path &p) {
    return {Status::Success, p, ""};
  }
  static SavePatchResult duplicate() { return {Status::Duplicate, {}, ""}; }
  static SavePatchResult error(const std::string &msg) {
    return {Status::Error, {}, msg};
  }
  static SavePatchResult unsupported() { return {}; }
};

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

  // Attempt to save a patch with the provided name. Default: unsupported.
  virtual SavePatchResult save_patch(const ym2612::Patch &, const std::string &,
                                     bool /*overwrite*/) {
    return SavePatchResult::unsupported();
  }

  // Optional metadata update. Default is unsupported (for storages without
  // metadata persistence).
  virtual bool update_patch_metadata(const std::string &,
                                     const PatchMetadata &) {
    return false;
  }

  // Optional path mapping helpers so PatchRepository doesn't need platform
  // checks. Return engaged optional if the storage owns the path.
  virtual std::optional<std::filesystem::path>
  to_relative_path(const std::filesystem::path &) const {
    return std::nullopt;
  }

  virtual std::optional<std::filesystem::path>
  to_absolute_path(const std::filesystem::path &) const {
    return std::nullopt;
  }
};

} // namespace patches
