#pragma once

#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace patches {

struct PatchMetadata {
  std::string path;     ///< Path relative to the folder that owns the sidecar.
  std::string hash;     ///< Hash of the patch content.
  int star_rating = 0;  ///< 0-5.
  std::string icon;     ///< Icon id, such as "organ".

  std::vector<std::string> tags;
  std::string notes;

  std::string created_at;
  std::string updated_at;

  /// Fields this struct does not model, written back unchanged.
  nlohmann::json extra = nlohmann::json::object();
};

/**
 * Star ratings and icons for one folder, stored in a JSON sidecar at
 * <folder>/.ym2612/patches.json, a file other tools share. Fields and
 * top-level keys this class does not model are kept as they are.
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
  /**
   * `legacy_sidecar_path`, when given, is read too: its fields fill those an
   * entry lacks, except `category`, which is dropped. The next save removes
   * it, and its directory if that is left empty.
   *
   * `defaults_path`, when given, is a sidecar read as a base layer and never
   * written. What get() and entries() return is each defaults entry with the
   * sidecar's fields laid over it. A save writes to the sidecar only the
   * fields that differ from the defaults; a field the defaults set and the
   * caller cleared is written with its empty value. An entry left with
   * nothing but timestamps is not written.
   */
  explicit FolderMetadataStore(std::filesystem::path sidecar_path,
                               std::filesystem::path legacy_sidecar_path = {},
                               std::filesystem::path defaults_path = {});

  /// Load the sidecar. A missing file is not an error -- it just means no
  /// metadata has been recorded yet. Returns false only on a corrupt file.
  bool load();

  std::optional<PatchMetadata> get(const std::string &relative_path) const;

  /// Insert or replace, stamping created_at/updated_at, then persist.
  bool put(PatchMetadata metadata);

  /// Every entry, in key order.
  std::vector<PatchMetadata> entries() const;

  /**
   * Take entries in as they are, timestamps included: one not present is
   * added, one present is replaced only by a later updated_at. Persisted in
   * one write.
   */
  bool merge_newer(const std::vector<PatchMetadata> &metadata,
                   std::size_t &changed);

  bool remove(const std::string &relative_path);

  /// Move one key and all descendant keys to a new path prefix, then persist
  /// the complete migration in one sidecar write.
  bool rename_key_prefix(const std::string &old_relative_path,
                         const std::string &new_relative_path);

  /// Drop entries whose patch file is no longer in `existing_paths`. The
  /// defaults are left as they are.
  bool retain_only(const std::vector<std::string> &existing_paths);

  const std::filesystem::path &sidecar_path() const { return sidecar_path_; }
  bool empty() const { return entries_.empty(); }

private:
  bool save();
  void remove_legacy_sidecar();
  void load_defaults();
  /// The defaults entry for `relative_path` alone, if there is one.
  std::optional<PatchMetadata>
  defaults_entry(const std::string &relative_path) const;
  /// What the sidecar holds for `metadata`; null when it holds nothing.
  nlohmann::json sidecar_entry(const PatchMetadata &metadata) const;

  std::filesystem::path sidecar_path_;
  std::filesystem::path legacy_sidecar_path_;
  std::filesystem::path defaults_path_;
  std::map<std::string, nlohmann::json> defaults_;
  bool legacy_loaded_ = false;
  nlohmann::json extra_ = nlohmann::json::object();
  std::map<std::string, PatchMetadata> entries_;
};

} // namespace patches
