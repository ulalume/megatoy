#pragma once

#include "patch_metadata.hpp"
#include "ym2612/patch.hpp"
#include <filesystem>
#include <optional>
#include <string_view>
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
  virtual SavePatchResult
  save_patch(const ym2612::Patch &, const std::string &, bool /*overwrite*/,
             std::string_view /*preferred_extension*/) {
    return SavePatchResult::unsupported();
  }

  // Optional metadata operations. Default is unsupported (for storages without
  // metadata persistence).
  virtual bool save_patch_metadata(const std::string &, const ym2612::Patch &,
                                   const PatchMetadata &) {
    return false;
  }

  virtual bool update_patch_metadata(const std::string &,
                                     const PatchMetadata &) {
    return false;
  }

  virtual std::optional<PatchMetadata>
  get_patch_metadata(const std::string &) const {
    return std::nullopt;
  }

  virtual void cleanup_metadata(const std::vector<std::string> &) const {}

  // Capability helpers for UI/logic to query storage properties.
  virtual bool is_writable() const { return false; }
  virtual std::string_view label() const { return {}; }

  // Optional duplicate check for a given patch name (unsanitized). If
  // unsupported, return std::nullopt.
  virtual std::optional<bool> has_patch_named(const std::string &) const {
    return std::nullopt;
  }

  // Optional download/export hook for storages that cannot map to filesystem
  // paths (e.g., web download). Returns success/failure.
  virtual bool download_patch(const ym2612::Patch &,
                              const std::string &name,
                              const std::string &extension_hint) {
    (void)name;
    (void)extension_hint;
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
