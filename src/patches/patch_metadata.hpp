#pragma once

#include <optional>
#include <string>
#include <vector>

namespace patches {

struct PatchMetadata {
  std::string path;     // Relative path to the patch file
  std::string hash;     // Hash of patch content
  int star_rating = 0;  // Star rating (0-5)
  std::string category; // Category string (bass, guitar, strings, etc.)

  // Optional fields for future expansion
  std::vector<std::string> tags;
  std::string notes;

  // Timestamps
  std::string created_at;
  std::string updated_at;
};

class PatchMetadataManager {
public:
  explicit PatchMetadataManager(const std::string &db_path);
  ~PatchMetadataManager();

  // Initialize database (create tables if needed)
  bool initialize();

  // CRUD operations
  bool save_metadata(const PatchMetadata &metadata);
  std::optional<PatchMetadata> get_metadata(const std::string &path);
  bool update_metadata(const PatchMetadata &metadata);
  bool delete_metadata(const std::string &path);

  // Hash-based operations for handling moved files
  std::optional<PatchMetadata> find_by_hash(const std::string &hash);
  bool update_path_by_hash(const std::string &hash,
                           const std::string &new_path);

  // Search and filtering
  std::vector<PatchMetadata> get_all_metadata();
  std::vector<PatchMetadata> get_by_category(const std::string &category);
  std::vector<PatchMetadata> get_by_star_rating(int min_rating);
  std::vector<PatchMetadata> get_by_tag(const std::string &tag);

  // Batch operations
  bool cleanup_missing_files(const std::vector<std::string> &existing_paths);

  // Statistics
  std::vector<std::string> get_all_categories();
  std::vector<std::string> get_all_tags();
  int get_total_patches();

private:
  class Impl;
  std::unique_ptr<Impl> pimpl_;
};

} // namespace patches
