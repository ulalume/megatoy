#pragma once

#include "workspace/workspace.hpp"
#include <cstddef>
#include <filesystem>
#include <string>

namespace patches {

struct LegacyMetadataMigrationResult {
  bool complete = false;
  std::size_t imported = 0;
  std::string error;
};

/**
 * Import the global SQLite metadata database used before workspace folders.
 *
 * The source is opened read-only and never removed. Existing JSON sidecar
 * entries are authoritative. If the old root is unknown, a workspace folder
 * is inferred only when a stored relative path exists in exactly one writable
 * folder; otherwise the migration remains pending for a later folder add.
 */
LegacyMetadataMigrationResult migrate_legacy_metadata(
    const std::filesystem::path &database_path,
    const megatoy::workspace::Workspace &workspace,
    const std::filesystem::path &preferred_workspace = {});

} // namespace patches
