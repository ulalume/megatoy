#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace patches {

struct PatchMetadata {
  std::string path;     ///< Path relative to the folder that owns the sidecar.
  std::string hash;     ///< Hash of the patch content.
  int star_rating = 0;  ///< 0-5.
  std::string category; ///< Free-form (bass, strings, ...).

  std::vector<std::string> tags;
  std::string notes;

  std::string created_at;
  std::string updated_at;
};

/**
 * Star ratings and categories for one folder, stored in a JSON sidecar at
 * <folder>/.megatoy/patches.json.
 *
 * Keeping this next to the patches, rather than in one global database keyed
 * by absolute path, means a folder can be moved, copied, synced or shared and
 * its ratings travel with it.
 *
 * Everything is held in memory and rewritten on change; a folder holds at
 * most a few thousand patches, so there is nothing to gain from a database.
 */
class FolderMetadataStore {
public:
  explicit FolderMetadataStore(std::filesystem::path sidecar_path);

  /// Load the sidecar. A missing file is not an error -- it just means no
  /// metadata has been recorded yet. Returns false only on a corrupt file.
  bool load();

  std::optional<PatchMetadata> get(const std::string &relative_path) const;

  /// Insert or replace, stamping created_at/updated_at, then persist.
  bool put(PatchMetadata metadata);

  bool remove(const std::string &relative_path);

  /// Drop entries whose patch file is no longer in `existing_paths`.
  bool retain_only(const std::vector<std::string> &existing_paths);

  const std::filesystem::path &sidecar_path() const { return sidecar_path_; }
  bool empty() const { return entries_.empty(); }

private:
  bool save() const;

  std::filesystem::path sidecar_path_;
  std::map<std::string, PatchMetadata> entries_;
};

} // namespace patches
