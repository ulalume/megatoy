#include "platform/web/web_storage_bootstrap.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include "core/status.hpp"
#include "formats/ym2612_format_adapter.hpp"
#include "patches/filename_utils.hpp"
#include "platform/web/local_storage.hpp"
#include "platform/web/web_storage_persistence.hpp"
#include "system/path_service.hpp"
#include "workspace/workspace.hpp"
#include "ym2612/patch.hpp"

#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace platform::web {

namespace {

// The pre-IDBFS build kept its entire patch library under this key.
constexpr const char *kLegacyKey = "megatoy_saved_patches";
constexpr const char *kMigrationManifestKey = "megatoy_saved_patches_migration";
constexpr int kMigrationVersion = 1;

struct LegacyPatch {
  std::string filename_stem;
  ym2612::Patch patch;
};

struct MigrationManifest {
  std::vector<std::string> filenames;
};

std::string source_fingerprint(const std::string &source) {
  // FNV-1a is deliberately implemented here instead of using std::hash:
  // this value must remain stable across builds and browser sessions.
  std::uint64_t hash = UINT64_C(14695981039346656037);
  for (const unsigned char byte : source) {
    hash ^= byte;
    hash *= UINT64_C(1099511628211);
  }

  std::ostringstream result;
  result << std::hex << std::setfill('0') << std::setw(16) << hash;
  return result.str();
}

std::optional<std::vector<LegacyPatch>>
parse_legacy_library(const std::string &source) {
  const auto json = nlohmann::json::parse(source);
  if (!json.is_array()) {
    return std::nullopt;
  }

  std::vector<LegacyPatch> patches;
  patches.reserve(json.size());
  for (const auto &item : json) {
    if (!item.is_object() || !item.contains("patch")) {
      return std::nullopt;
    }

    LegacyPatch entry;
    std::string name = item.value("name", std::string{});
    if (name.empty()) {
      name = item.value("id", std::string{});
    }
    if (name.empty()) {
      name = "patch";
    }

    entry.patch = item.at("patch").get<ym2612::Patch>();
    entry.filename_stem = patches::sanitize_filename(name);
    if (entry.filename_stem.empty()) {
      entry.filename_stem = "patch";
    }
    entry.patch.name = entry.filename_stem;
    patches.push_back(std::move(entry));
  }
  return patches;
}

bool is_safe_manifest_filename(const std::string &filename) {
  if (filename.empty() || filename.find('/') != std::string::npos ||
      filename.find('\\') != std::string::npos) {
    return false;
  }

  const std::filesystem::path path(filename);
  if (path.is_absolute() || path.has_parent_path() ||
      path.extension() != ".gin") {
    return false;
  }

  const auto stem = path.stem().string();
  return !stem.empty() && patches::sanitize_filename(stem) == stem;
}

std::optional<MigrationManifest> parse_manifest(const std::string &stored,
                                                const std::string &source,
                                                std::size_t patch_count) {
  try {
    const auto json = nlohmann::json::parse(stored);
    if (!json.is_object() || json.value("version", 0) != kMigrationVersion ||
        json.value("source_size", std::size_t{0}) != source.size() ||
        json.value("source_fingerprint", std::string{}) !=
            source_fingerprint(source) ||
        !json.contains("filenames") || !json.at("filenames").is_array() ||
        json.at("filenames").size() != patch_count) {
      return std::nullopt;
    }

    MigrationManifest manifest;
    std::set<std::string> unique;
    manifest.filenames.reserve(patch_count);
    for (const auto &entry : json.at("filenames")) {
      if (!entry.is_string()) {
        return std::nullopt;
      }
      auto filename = entry.get<std::string>();
      if (!is_safe_manifest_filename(filename) ||
          !unique.insert(filename).second) {
        return std::nullopt;
      }
      manifest.filenames.push_back(std::move(filename));
    }
    return manifest;
  } catch (...) {
    return std::nullopt;
  }
}

bool store_manifest(const MigrationManifest &manifest,
                    const std::string &source) {
  nlohmann::json json{{"version", kMigrationVersion},
                      {"source_size", source.size()},
                      {"source_fingerprint", source_fingerprint(source)},
                      {"filenames", manifest.filenames}};
  return write_local_storage(kMigrationManifestKey, json.dump());
}

bool target_matches(const std::filesystem::path &target,
                    const ym2612::Patch &expected) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(target, ec) || ec) {
    return false;
  }
  const auto loaded =
      formats::adapter::read_file(ym2612_format::Format::Gin, target);
  return loaded.size() == 1 && loaded.front() == expected;
}

std::string numbered_filename(const std::string &stem, std::size_t number) {
  if (number == 1) {
    return stem + ".gin";
  }
  return stem + " (" + std::to_string(number) + ").gin";
}

std::optional<std::string>
choose_filename(const std::filesystem::path &destination,
                const LegacyPatch &entry,
                const std::set<std::string> &reserved) {
  for (std::size_t number = 1; number != 0; ++number) {
    auto filename = numbered_filename(entry.filename_stem, number);
    if (reserved.contains(filename)) {
      continue;
    }

    const auto target = destination / filename;
    std::error_code ec;
    const bool exists = std::filesystem::exists(target, ec);
    if (ec) {
      return std::nullopt;
    }
    if (!exists || target_matches(target, entry.patch)) {
      return filename;
    }
  }
  return std::nullopt;
}

std::optional<MigrationManifest>
plan_migration(const std::filesystem::path &destination,
               const std::vector<LegacyPatch> &patches) {
  MigrationManifest manifest;
  std::set<std::string> reserved;
  manifest.filenames.reserve(patches.size());
  for (const auto &entry : patches) {
    auto filename = choose_filename(destination, entry, reserved);
    if (!filename) {
      return std::nullopt;
    }
    reserved.insert(*filename);
    manifest.filenames.push_back(std::move(*filename));
  }
  return manifest;
}

std::optional<std::filesystem::path>
migration_temp_path(const std::filesystem::path &target) {
  const auto prefix = "." + target.filename().string() + ".migration";
  for (std::size_t number = 1; number != 0; ++number) {
    const auto filename =
        number == 1 ? prefix : prefix + "." + std::to_string(number);
    const auto candidate = target.parent_path() / filename;
    std::error_code ec;
    const bool exists = std::filesystem::exists(candidate, ec);
    if (ec) {
      return std::nullopt;
    }
    if (!exists) {
      return candidate;
    }
  }
  return std::nullopt;
}

bool write_migrated_patch(const std::filesystem::path &target,
                          const ym2612::Patch &patch) {
  const auto temporary = migration_temp_path(target);
  if (!temporary) {
    return false;
  }

  if (!formats::adapter::write_file(ym2612_format::Format::Gin, patch,
                                    *temporary) ||
      !target_matches(*temporary, patch)) {
    std::error_code cleanup_error;
    std::filesystem::remove(*temporary, cleanup_error);
    return false;
  }

  // Never replace a file that appeared after the manifest was planned.
  std::error_code ec;
  if (std::filesystem::exists(target, ec) || ec) {
    std::filesystem::remove(*temporary, ec);
    return false;
  }

  std::filesystem::rename(*temporary, target, ec);
  if (ec) {
    std::error_code cleanup_error;
    std::filesystem::remove(*temporary, cleanup_error);
    return false;
  }
  return true;
}

void report_migration_error(const std::string &message) {
  megatoy::status::error("Could not migrate saved patches: " + message);
}

/**
 * Rewrite the old localStorage library as real files using a two-startup
 * transaction.
 *
 * The manifest is committed to localStorage before any IDBFS writes. The old
 * blob remains authoritative until a later startup (after FS.syncfs(true))
 * reads every GIN back and proves that it is semantically identical.
 */
std::size_t migrate_legacy_library(const std::filesystem::path &destination) {
  const auto source = read_local_storage(kLegacyKey);
  if (!source.has_value()) {
    // read_local_storage intentionally treats blocked storage like a missing
    // key. Do not touch a possibly orphaned manifest in that ambiguous case.
    return 0;
  }

  std::vector<LegacyPatch> patches;
  try {
    auto parsed = parse_legacy_library(*source);
    if (!parsed) {
      report_migration_error("the old library is not a valid patch list");
      return 0;
    }
    patches = std::move(*parsed);
  } catch (const std::exception &error) {
    report_migration_error(error.what());
    return 0;
  }

  MigrationManifest manifest;
  bool continuing = false;
  if (const auto stored_manifest = read_local_storage(kMigrationManifestKey)) {
    if (auto parsed =
            parse_manifest(*stored_manifest, *source, patches.size())) {
      manifest = std::move(*parsed);
      continuing = true;
    }
  }

  if (!continuing) {
    auto planned = plan_migration(destination, patches);
    if (!planned) {
      report_migration_error("could not reserve destination filenames");
      return 0;
    }
    manifest = std::move(*planned);
    if (!store_manifest(manifest, *source)) {
      report_migration_error("could not save the migration manifest");
      return 0;
    }
  }

  std::vector<bool> matches(patches.size(), false);
  bool all_match = true;
  for (std::size_t index = 0; index < patches.size(); ++index) {
    matches[index] = target_matches(destination / manifest.filenames[index],
                                    patches[index].patch);
    all_match = all_match && matches[index];
  }

  if (continuing && all_match) {
    // Source first, manifest second: if deletion is interrupted, the mapping
    // needed for a safe retry is never lost while the source still exists.
    // The conditional delete also protects a newer save made by an old tab.
    if (remove_local_storage_if_equals(kLegacyKey, *source)) {
      remove_local_storage(kMigrationManifestKey);
    }
    return 0;
  }

  // An existing non-matching target may belong to the user. Move only that
  // manifest entry to a fresh filename, and persist the new mapping before
  // writing any files.
  std::set<std::string> reserved(manifest.filenames.begin(),
                                 manifest.filenames.end());
  bool manifest_changed = false;
  for (std::size_t index = 0; index < patches.size(); ++index) {
    if (matches[index]) {
      continue;
    }

    const auto target = destination / manifest.filenames[index];
    std::error_code ec;
    const bool exists = std::filesystem::exists(target, ec);
    if (ec) {
      report_migration_error(ec.message());
      return 0;
    }
    if (!exists) {
      continue;
    }

    auto replacement = choose_filename(destination, patches[index], reserved);
    if (!replacement) {
      report_migration_error("could not reserve a replacement filename");
      return 0;
    }
    reserved.insert(*replacement);
    manifest.filenames[index] = std::move(*replacement);
    manifest_changed = true;
  }

  if (manifest_changed && !store_manifest(manifest, *source)) {
    report_migration_error("could not update the migration manifest");
    return 0;
  }

  std::size_t migrated = 0;
  bool write_failed = false;
  for (std::size_t index = 0; index < patches.size(); ++index) {
    const auto target = destination / manifest.filenames[index];
    if (target_matches(target, patches[index].patch)) {
      continue;
    }
    if (write_migrated_patch(target, patches[index].patch)) {
      ++migrated;
    } else {
      write_failed = true;
    }
  }

  if (write_failed) {
    report_migration_error("one or more files could not be written");
  } else if (migrated > 0) {
    megatoy::status::success("Moved " + std::to_string(migrated) +
                             " saved patch(es) into \"My Patches\".");
  }

  // Even after a successful read-back, this startup has only proved the
  // in-memory MEMFS state. Keep source + manifest until the next syncfs(true).
  return migrated;
}

} // namespace

std::filesystem::path default_workspace_folder() {
  return megatoy::system::PathService::web_storage_root() / kDefaultFolderName;
}

bool bootstrap_workspace(megatoy::workspace::Workspace &workspace,
                         const std::filesystem::path &storage_root) {
  if (storage_load_failed()) {
    megatoy::status::error(
        "Could not read browser storage -- running without saving. Your "
        "stored patches are untouched; reload to retry.");
    if (storage_root.empty()) {
      return false;
    }

    std::error_code ec;
    const auto home = storage_root / kDefaultFolderName;
    std::filesystem::create_directories(home, ec);
    if (ec) {
      return false;
    }
    return workspace.contains(home) ? false : workspace.add(home);
  }

  if (storage_root.empty()) {
    return false;
  }

  std::error_code ec;
  std::filesystem::create_directories(storage_root, ec);
  if (ec) {
    megatoy::status::error(
        "Persistent storage unavailable -- changes will not survive a "
        "reload. (" +
        ec.message() + ")");
    return false;
  }

  const auto home = storage_root / kDefaultFolderName;
  const bool existed = std::filesystem::is_directory(home, ec);
  if (!existed) {
    std::filesystem::create_directories(home, ec);
    if (ec) {
      megatoy::status::error("Could not create \"My Patches\": " +
                             ec.message());
      return false;
    }
  }

  const std::size_t migrated = migrate_legacy_library(home);

  bool changed = false;
  if (!workspace.contains(home)) {
    changed = workspace.add(home);
  }

  // Folders imported in an earlier session are still on disk but only
  // referenced from preferences. If preferences were cleared, re-adopt them so
  // the library is not silently invisible.
  std::filesystem::directory_iterator entry(storage_root, ec);
  const std::filesystem::directory_iterator end;
  while (!ec && entry != end) {
    std::error_code type_error;
    const bool is_directory = entry->is_directory(type_error);
    if (type_error || !is_directory || workspace.contains(entry->path())) {
      entry.increment(ec);
      continue;
    }
    if (workspace.add(entry->path())) {
      changed = true;
    }
    entry.increment(ec);
  }

  if (changed || migrated > 0 || !existed) {
    request_storage_persist();
  }
  return changed || migrated > 0;
}

} // namespace platform::web

#endif
