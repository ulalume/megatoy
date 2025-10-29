#include "patch_metadata.hpp"
#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Exception.h>
#include <SQLiteCpp/Statement.h>
#include <SQLiteCpp/Transaction.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace patches {

namespace {

constexpr const char kPatchMetadataTableColumns[] = R"(path TEXT PRIMARY KEY,
  hash TEXT NOT NULL,
  star_rating INTEGER DEFAULT 0 CHECK (star_rating >= 0 AND star_rating <= 5),
  category TEXT DEFAULT '',
  notes TEXT DEFAULT '',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
)";

} // namespace

class PatchMetadataManager::Impl {
public:
  explicit Impl(const std::string &db_path) : db_path_(db_path) {}

  bool initialize() {
    try {
      // Open database (creates if doesn't exist)
      db_ = std::make_unique<SQLite::Database>(
          db_path_, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);

      // Create tables if they don't exist
      create_tables();

      return true;
    } catch (const SQLite::Exception &e) {
      std::cerr << "Failed to initialize patch metadata database: " << e.what()
                << std::endl;
      return false;
    }
  }

  bool save_metadata(const PatchMetadata &metadata) {
    try {
      auto now = get_current_timestamp();

      SQLite::Statement query(
          *db_,
          "INSERT OR REPLACE INTO patch_metadata "
          "(path, hash, star_rating, category, notes, created_at, updated_at) "
          "VALUES (?, ?, ?, ?, ?, "
          "COALESCE((SELECT created_at FROM patch_metadata WHERE path = ?), "
          "?), ?)");

      const int clamped_rating = std::clamp(metadata.star_rating, 0, 5);

      query.bind(1, metadata.path);
      query.bind(2, metadata.hash);
      query.bind(3, clamped_rating);
      query.bind(4, metadata.category);
      query.bind(5, metadata.notes);
      query.bind(6, metadata.path); // for COALESCE
      query.bind(7, now);           // created_at fallback
      query.bind(8, now);           // updated_at

      query.exec();

      // Handle tags separately
      save_tags(metadata.path, metadata.tags);

      return true;
    } catch (const SQLite::Exception &e) {
      std::cerr << "Failed to save patch metadata: " << e.what() << std::endl;
      return false;
    }
  }

  std::optional<PatchMetadata> get_metadata(const std::string &path) {
    try {
      SQLite::Statement query(*db_, "SELECT path, hash, star_rating, category, "
                                    "notes, created_at, updated_at "
                                    "FROM patch_metadata WHERE path = ?");
      query.bind(1, path);

      if (query.executeStep()) {
        PatchMetadata metadata;
        metadata.path = query.getColumn(0).getString();
        metadata.hash = query.getColumn(1).getString();
        metadata.star_rating = query.getColumn(2).getInt();
        metadata.category = query.getColumn(3).getString();
        metadata.notes = query.getColumn(4).getString();
        metadata.created_at = query.getColumn(5).getString();
        metadata.updated_at = query.getColumn(6).getString();

        // Load tags
        metadata.tags = get_tags(path);

        return metadata;
      }
    } catch (const SQLite::Exception &e) {
      std::cerr << "Failed to get patch metadata: " << e.what() << std::endl;
    }
    return std::nullopt;
  }

  bool update_metadata(const PatchMetadata &metadata) {
    return save_metadata(metadata); // INSERT OR REPLACE handles updates
  }

  bool delete_metadata(const std::string &path) {
    try {
      SQLite::Transaction transaction(*db_);

      // Delete tags first
      SQLite::Statement delete_tags(*db_,
                                    "DELETE FROM patch_tags WHERE path = ?");
      delete_tags.bind(1, path);
      delete_tags.exec();

      // Delete metadata
      SQLite::Statement delete_metadata(
          *db_, "DELETE FROM patch_metadata WHERE path = ?");
      delete_metadata.bind(1, path);
      delete_metadata.exec();

      transaction.commit();
      return true;
    } catch (const SQLite::Exception &e) {
      std::cerr << "Failed to delete patch metadata: " << e.what() << std::endl;
      return false;
    }
  }

  std::optional<PatchMetadata> find_by_hash(const std::string &hash) {
    try {
      SQLite::Statement query(*db_, "SELECT path, hash, star_rating, category, "
                                    "notes, created_at, updated_at "
                                    "FROM patch_metadata WHERE hash = ?");
      query.bind(1, hash);

      if (query.executeStep()) {
        PatchMetadata metadata;
        metadata.path = query.getColumn(0).getString();
        metadata.hash = query.getColumn(1).getString();
        metadata.star_rating = query.getColumn(2).getInt();
        metadata.category = query.getColumn(3).getString();
        metadata.notes = query.getColumn(4).getString();
        metadata.created_at = query.getColumn(5).getString();
        metadata.updated_at = query.getColumn(6).getString();

        // Load tags
        metadata.tags = get_tags(metadata.path);

        return metadata;
      }
    } catch (const SQLite::Exception &e) {
      std::cerr << "Failed to find patch by hash: " << e.what() << std::endl;
    }
    return std::nullopt;
  }

  bool update_path_by_hash(const std::string &hash,
                           const std::string &new_path) {
    try {
      SQLite::Transaction transaction(*db_);

      // Get old path first
      SQLite::Statement get_old_path(
          *db_, "SELECT path FROM patch_metadata WHERE hash = ?");
      get_old_path.bind(1, hash);

      if (get_old_path.executeStep()) {
        std::string old_path = get_old_path.getColumn(0).getString();

        // Update metadata path
        SQLite::Statement update_metadata(*db_,
                                          "UPDATE patch_metadata SET path = ?, "
                                          "updated_at = ? WHERE hash = ?");
        update_metadata.bind(1, new_path);
        update_metadata.bind(2, get_current_timestamp());
        update_metadata.bind(3, hash);
        update_metadata.exec();

        // Update tags path
        SQLite::Statement update_tags(
            *db_, "UPDATE patch_tags SET path = ? WHERE path = ?");
        update_tags.bind(1, new_path);
        update_tags.bind(2, old_path);
        update_tags.exec();

        transaction.commit();
        return true;
      }
    } catch (const SQLite::Exception &e) {
      std::cerr << "Failed to update path by hash: " << e.what() << std::endl;
    }
    return false;
  }

  std::vector<PatchMetadata> get_all_metadata() {
    std::vector<PatchMetadata> results;
    try {
      SQLite::Statement query(*db_, "SELECT path, hash, star_rating, category, "
                                    "notes, created_at, updated_at "
                                    "FROM patch_metadata ORDER BY path");

      while (query.executeStep()) {
        PatchMetadata metadata;
        metadata.path = query.getColumn(0).getString();
        metadata.hash = query.getColumn(1).getString();
        metadata.star_rating = query.getColumn(2).getInt();
        metadata.category = query.getColumn(3).getString();
        metadata.notes = query.getColumn(4).getString();
        metadata.created_at = query.getColumn(5).getString();
        metadata.updated_at = query.getColumn(6).getString();

        // Load tags
        metadata.tags = get_tags(metadata.path);

        results.push_back(metadata);
      }
    } catch (const SQLite::Exception &e) {
      std::cerr << "Failed to get all metadata: " << e.what() << std::endl;
    }
    return results;
  }

  std::vector<PatchMetadata> get_by_category(const std::string &category) {
    std::vector<PatchMetadata> results;
    try {
      SQLite::Statement query(
          *db_, "SELECT path, hash, star_rating, category, "
                "notes, created_at, updated_at "
                "FROM patch_metadata WHERE category = ? ORDER BY path");
      query.bind(1, category);

      while (query.executeStep()) {
        PatchMetadata metadata;
        metadata.path = query.getColumn(0).getString();
        metadata.hash = query.getColumn(1).getString();
        metadata.star_rating = query.getColumn(2).getInt();
        metadata.category = query.getColumn(3).getString();
        metadata.notes = query.getColumn(4).getString();
        metadata.created_at = query.getColumn(5).getString();
        metadata.updated_at = query.getColumn(6).getString();

        metadata.tags = get_tags(metadata.path);
        results.push_back(metadata);
      }
    } catch (const SQLite::Exception &e) {
      std::cerr << "Failed to get metadata by category: " << e.what()
                << std::endl;
    }
    return results;
  }

  std::vector<PatchMetadata> get_by_star_rating(int min_rating) {
    std::vector<PatchMetadata> results;
    try {
      SQLite::Statement query(*db_,
                              "SELECT path, hash, star_rating, category, "
                              "notes, created_at, updated_at "
                              "FROM patch_metadata WHERE star_rating >= ? "
                              "ORDER BY star_rating DESC, path");
      query.bind(1, min_rating);

      while (query.executeStep()) {
        PatchMetadata metadata;
        metadata.path = query.getColumn(0).getString();
        metadata.hash = query.getColumn(1).getString();
        metadata.star_rating = query.getColumn(2).getInt();
        metadata.category = query.getColumn(3).getString();
        metadata.notes = query.getColumn(4).getString();
        metadata.created_at = query.getColumn(5).getString();
        metadata.updated_at = query.getColumn(6).getString();

        metadata.tags = get_tags(metadata.path);
        results.push_back(metadata);
      }
    } catch (const SQLite::Exception &e) {
      std::cerr << "Failed to get metadata by star rating: " << e.what()
                << std::endl;
    }
    return results;
  }

  std::vector<PatchMetadata> get_by_tag(const std::string &tag) {
    std::vector<PatchMetadata> results;
    try {
      SQLite::Statement query(
          *db_, "SELECT DISTINCT pm.path, pm.hash, pm.star_rating, "
                "pm.category, pm.notes, pm.created_at, pm.updated_at "
                "FROM patch_metadata pm "
                "JOIN patch_tags pt ON pm.path = pt.path "
                "WHERE pt.tag = ? ORDER BY pm.path");
      query.bind(1, tag);

      while (query.executeStep()) {
        PatchMetadata metadata;
        metadata.path = query.getColumn(0).getString();
        metadata.hash = query.getColumn(1).getString();
        metadata.star_rating = query.getColumn(2).getInt();
        metadata.category = query.getColumn(3).getString();
        metadata.notes = query.getColumn(4).getString();
        metadata.created_at = query.getColumn(5).getString();
        metadata.updated_at = query.getColumn(6).getString();

        metadata.tags = get_tags(metadata.path);
        results.push_back(metadata);
      }
    } catch (const SQLite::Exception &e) {
      std::cerr << "Failed to get metadata by tag: " << e.what() << std::endl;
    }
    return results;
  }

  bool cleanup_missing_files(const std::vector<std::string> &existing_paths) {
    try {
      SQLite::Transaction transaction(*db_);

      // Create temporary table with existing paths
      db_->exec("CREATE TEMP TABLE existing_paths (path TEXT PRIMARY KEY)");

      SQLite::Statement insert_existing(
          *db_, "INSERT INTO existing_paths (path) VALUES (?)");
      for (const auto &path : existing_paths) {
        insert_existing.bind(1, path);
        insert_existing.exec();
        insert_existing.reset();
      }

      // Delete metadata for non-existing files
      db_->exec("DELETE FROM patch_tags WHERE path NOT IN "
                "(SELECT path FROM existing_paths)");
      db_->exec("DELETE FROM patch_metadata WHERE path NOT IN "
                "(SELECT path FROM existing_paths)");

      // Drop temporary table
      db_->exec("DROP TABLE existing_paths");

      transaction.commit();
      return true;
    } catch (const SQLite::Exception &e) {
      std::cerr << "Failed to cleanup missing files: " << e.what() << std::endl;
      return false;
    }
  }

  std::vector<std::string> get_all_categories() {
    std::vector<std::string> categories;
    try {
      SQLite::Statement query(*db_,
                              "SELECT DISTINCT category FROM patch_metadata "
                              "WHERE category != '' ORDER BY category");

      while (query.executeStep()) {
        categories.push_back(query.getColumn(0).getString());
      }
    } catch (const SQLite::Exception &e) {
      std::cerr << "Failed to get categories: " << e.what() << std::endl;
    }
    return categories;
  }

  std::vector<std::string> get_all_tags() {
    std::vector<std::string> tags;
    try {
      SQLite::Statement query(
          *db_, "SELECT DISTINCT tag FROM patch_tags ORDER BY tag");

      while (query.executeStep()) {
        tags.push_back(query.getColumn(0).getString());
      }
    } catch (const SQLite::Exception &e) {
      std::cerr << "Failed to get tags: " << e.what() << std::endl;
    }
    return tags;
  }

  int get_total_patches() {
    try {
      SQLite::Statement query(*db_, "SELECT COUNT(*) FROM patch_metadata");
      if (query.executeStep()) {
        return query.getColumn(0).getInt();
      }
    } catch (const SQLite::Exception &e) {
      std::cerr << "Failed to get total patches: " << e.what() << std::endl;
    }
    return 0;
  }

private:
  std::string db_path_;
  std::unique_ptr<SQLite::Database> db_;

  void create_tables() {
    migrate_patch_metadata_star_limit();

    db_->exec(std::string("CREATE TABLE IF NOT EXISTS patch_metadata (\n") +
              kPatchMetadataTableColumns + "\n)");
    create_patch_metadata_indexes();

    db_->exec(R"(
      CREATE TABLE IF NOT EXISTS patch_tags (
        path TEXT NOT NULL,
        tag TEXT NOT NULL,
        PRIMARY KEY (path, tag),
        FOREIGN KEY (path) REFERENCES patch_metadata(path) ON DELETE CASCADE
      )
    )");
    create_patch_tags_indexes();
  }

  std::string get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
  }

  void save_tags(const std::string &path,
                 const std::vector<std::string> &tags) {
    try {
      // Delete existing tags
      SQLite::Statement delete_tags(*db_,
                                    "DELETE FROM patch_tags WHERE path = ?");
      delete_tags.bind(1, path);
      delete_tags.exec();

      // Insert new tags
      if (!tags.empty()) {
        SQLite::Statement insert_tag(
            *db_, "INSERT INTO patch_tags (path, tag) VALUES (?, ?)");
        for (const auto &tag : tags) {
          if (!tag.empty()) {
            insert_tag.bind(1, path);
            insert_tag.bind(2, tag);
            insert_tag.exec();
            insert_tag.reset();
          }
        }
      }
    } catch (const SQLite::Exception &e) {
      std::cerr << "Failed to save tags: " << e.what() << std::endl;
    }
  }

  std::vector<std::string> get_tags(const std::string &path) {
    std::vector<std::string> tags;
    try {
      SQLite::Statement query(
          *db_, "SELECT tag FROM patch_tags WHERE path = ? ORDER BY tag");
      query.bind(1, path);

      while (query.executeStep()) {
        tags.push_back(query.getColumn(0).getString());
      }
    } catch (const SQLite::Exception &e) {
      std::cerr << "Failed to get tags: " << e.what() << std::endl;
    }
    return tags;
  }

  void create_patch_metadata_indexes() {
    try {
      db_->exec("CREATE INDEX IF NOT EXISTS idx_patch_metadata_hash ON "
                "patch_metadata(hash)");
      db_->exec("CREATE INDEX IF NOT EXISTS idx_patch_metadata_category ON "
                "patch_metadata(category)");
      db_->exec("CREATE INDEX IF NOT EXISTS idx_patch_metadata_star_rating ON "
                "patch_metadata(star_rating)");
    } catch (const SQLite::Exception &e) {
      std::cerr << "Failed to create patch metadata indexes: " << e.what()
                << std::endl;
    }
  }

  void create_patch_tags_indexes() {
    try {
      db_->exec(
          "CREATE INDEX IF NOT EXISTS idx_patch_tags_tag ON patch_tags(tag)");
    } catch (const SQLite::Exception &e) {
      std::cerr << "Failed to create patch tag indexes: " << e.what()
                << std::endl;
    }
  }

  void migrate_patch_metadata_star_limit() {
    try {
      SQLite::Statement stmt(
          *db_, "SELECT sql FROM sqlite_master WHERE type='table' AND "
                "name='patch_metadata'");
      if (!stmt.executeStep()) {
        return;
      }
      std::string table_sql = stmt.getColumn(0).getString();
      std::string normalized;
      normalized.reserve(table_sql.size());
      for (char ch : table_sql) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
          normalized.push_back(static_cast<char>(std::tolower(ch)));
        }
      }

      if (normalized.find("starrating>=0andstarrating<=4") ==
          std::string::npos) {
        return;
      }

      db_->exec("BEGIN TRANSACTION;");
      try {
        db_->exec("ALTER TABLE patch_metadata RENAME TO patch_metadata_old;");
        db_->exec(std::string("CREATE TABLE patch_metadata (\n") +
                  kPatchMetadataTableColumns + "\n)");
        db_->exec(
            "INSERT INTO patch_metadata (path, hash, star_rating, category, "
            "notes, created_at, updated_at) "
            "SELECT path, hash, star_rating, category, notes, created_at, "
            "updated_at FROM patch_metadata_old");
        db_->exec("DROP TABLE patch_metadata_old;");
        create_patch_metadata_indexes();
        db_->exec("COMMIT;");
      } catch (const SQLite::Exception &) {
        db_->exec("ROLLBACK;");
        throw;
      }
    } catch (const SQLite::Exception &e) {
      std::cerr << "Failed to migrate patch metadata table: " << e.what()
                << std::endl;
    }
  }
};

// PatchMetadataManager implementation
PatchMetadataManager::PatchMetadataManager(const std::string &db_path)
    : pimpl_(std::make_unique<Impl>(db_path)) {}

PatchMetadataManager::~PatchMetadataManager() = default;

bool PatchMetadataManager::initialize() { return pimpl_->initialize(); }

bool PatchMetadataManager::save_metadata(const PatchMetadata &metadata) {
  return pimpl_->save_metadata(metadata);
}

std::optional<PatchMetadata>
PatchMetadataManager::get_metadata(const std::string &path) {
  return pimpl_->get_metadata(path);
}

bool PatchMetadataManager::update_metadata(const PatchMetadata &metadata) {
  return pimpl_->update_metadata(metadata);
}

bool PatchMetadataManager::delete_metadata(const std::string &path) {
  return pimpl_->delete_metadata(path);
}

std::optional<PatchMetadata>
PatchMetadataManager::find_by_hash(const std::string &hash) {
  return pimpl_->find_by_hash(hash);
}

bool PatchMetadataManager::update_path_by_hash(const std::string &hash,
                                               const std::string &new_path) {
  return pimpl_->update_path_by_hash(hash, new_path);
}

std::vector<PatchMetadata> PatchMetadataManager::get_all_metadata() {
  return pimpl_->get_all_metadata();
}

std::vector<PatchMetadata>
PatchMetadataManager::get_by_category(const std::string &category) {
  return pimpl_->get_by_category(category);
}

std::vector<PatchMetadata>
PatchMetadataManager::get_by_star_rating(int min_rating) {
  return pimpl_->get_by_star_rating(min_rating);
}

std::vector<PatchMetadata>
PatchMetadataManager::get_by_tag(const std::string &tag) {
  return pimpl_->get_by_tag(tag);
}

bool PatchMetadataManager::cleanup_missing_files(
    const std::vector<std::string> &existing_paths) {
  return pimpl_->cleanup_missing_files(existing_paths);
}

std::vector<std::string> PatchMetadataManager::get_all_categories() {
  return pimpl_->get_all_categories();
}

std::vector<std::string> PatchMetadataManager::get_all_tags() {
  return pimpl_->get_all_tags();
}

int PatchMetadataManager::get_total_patches() {
  return pimpl_->get_total_patches();
}

} // namespace patches
