#include "patches/legacy_metadata_migration.hpp"

#include "patches/folder_metadata.hpp"
#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Exception.h>
#include <SQLiteCpp/Statement.h>
#include <algorithm>
#include <filesystem>
#include <map>
#include <system_error>

namespace patches {
namespace {

namespace fs = std::filesystem;

bool safe_relative_path(const std::string &path) {
  if (path.empty()) {
    return false;
  }
  const fs::path parsed(path);
  if (parsed.is_absolute()) {
    return false;
  }
  return std::none_of(parsed.begin(), parsed.end(),
                      [](const fs::path &part) { return part == ".."; });
}

bool is_builtin_path(const std::string &path) {
  return path == "presets" || path.rfind("presets/", 0) == 0;
}

bool has_table(SQLite::Database &database, const char *name) {
  SQLite::Statement query(
      database,
      "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?");
  query.bind(1, name);
  return query.executeStep();
}

std::vector<PatchMetadata> read_metadata(SQLite::Database &database) {
  std::map<std::string, PatchMetadata> entries;
  SQLite::Statement metadata(
      database,
      "SELECT path, hash, star_rating, category, notes, created_at, updated_at "
      "FROM patch_metadata ORDER BY path");
  while (metadata.executeStep()) {
    PatchMetadata entry;
    entry.path = metadata.getColumn(0).getString();
    if (!safe_relative_path(entry.path) || is_builtin_path(entry.path)) {
      continue;
    }
    entry.hash = metadata.getColumn(1).getString();
    entry.star_rating = std::clamp(metadata.getColumn(2).getInt(), 0, 5);
    entry.category = metadata.getColumn(3).getString();
    entry.notes = metadata.getColumn(4).getString();
    entry.created_at = metadata.getColumn(5).getString();
    entry.updated_at = metadata.getColumn(6).getString();
    entries.emplace(entry.path, std::move(entry));
  }

  if (has_table(database, "patch_tags")) {
    SQLite::Statement tags(
        database, "SELECT path, tag FROM patch_tags ORDER BY path, tag");
    while (tags.executeStep()) {
      const std::string path = tags.getColumn(0).getString();
      const std::string tag = tags.getColumn(1).getString();
      if (const auto found = entries.find(path);
          found != entries.end() && !tag.empty()) {
        found->second.tags.push_back(tag);
      }
    }
  }

  std::vector<PatchMetadata> result;
  result.reserve(entries.size());
  for (auto &[path, entry] : entries) {
    result.push_back(std::move(entry));
  }
  return result;
}

const megatoy::workspace::Folder *find_folder(
    const megatoy::workspace::Workspace &workspace, const fs::path &path) {
  if (path.empty()) {
    return nullptr;
  }
  std::error_code ec;
  auto normalized = fs::weakly_canonical(path, ec);
  if (ec) {
    normalized = path.lexically_normal();
  }
  for (const auto &folder : workspace.folders()) {
    if (folder.path == normalized && folder.available && folder.writable) {
      return &folder;
    }
  }
  return nullptr;
}

const megatoy::workspace::Folder *infer_folder(
    const megatoy::workspace::Workspace &workspace,
    const std::vector<PatchMetadata> &metadata) {
  const megatoy::workspace::Folder *match = nullptr;
  for (const auto &folder : workspace.folders()) {
    if (!folder.available || !folder.writable) {
      continue;
    }
    bool contains_entry = false;
    for (const auto &entry : metadata) {
      std::error_code ec;
      if (fs::exists(folder.path / fs::path(entry.path), ec) && !ec) {
        contains_entry = true;
        break;
      }
    }
    if (!contains_entry) {
      continue;
    }
    if (match != nullptr) {
      return nullptr;
    }
    match = &folder;
  }
  return match;
}

} // namespace

LegacyMetadataMigrationResult migrate_legacy_metadata(
    const fs::path &database_path,
    const megatoy::workspace::Workspace &workspace,
    const fs::path &preferred_workspace) {
  LegacyMetadataMigrationResult result;
  std::error_code ec;
  if (!fs::exists(database_path, ec)) {
    result.complete = !ec;
    if (ec) {
      result.error = ec.message();
    }
    return result;
  }

  try {
    SQLite::Database database(database_path.string(), SQLite::OPEN_READONLY);
    if (!has_table(database, "patch_metadata")) {
      result.complete = true;
      return result;
    }
    auto metadata = read_metadata(database);
    if (metadata.empty()) {
      result.complete = true;
      return result;
    }

    const auto *folder = find_folder(workspace, preferred_workspace);
    if (folder == nullptr) {
      folder = infer_folder(workspace, metadata);
    }
    if (folder == nullptr) {
      // A custom legacy folder may only be added later. Keep retrying rather
      // than declaring success and losing the only automatic recovery chance.
      return result;
    }

    FolderMetadataStore destination(folder->metadata_path());
    if (!destination.load()) {
      result.error = "could not read the existing metadata sidecar";
      return result;
    }
    if (!destination.merge_missing(metadata, result.imported)) {
      result.error = "could not write the metadata sidecar";
      return result;
    }
    result.complete = true;
    return result;
  } catch (const SQLite::Exception &error) {
    result.error = error.what();
    return result;
  }
}

} // namespace patches
