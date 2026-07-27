#include "platform/web/web_storage_bootstrap.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include "formats/ym2612_format_adapter.hpp"
#include "patches/filename_utils.hpp"
#include "platform/web/local_storage.hpp"
#include "workspace/workspace.hpp"
#include "ym2612/patch.hpp"

#include <iostream>
#include <nlohmann/json.hpp>
#include <system_error>

namespace platform::web {

namespace {

// The key the pre-IDBFS build kept its entire patch library under.
constexpr const char *kLegacyKey = "megatoy_saved_patches";

/**
 * Rewrite the old localStorage library as real files.
 *
 * That store held every patch in one JSON array under one key, so each save
 * rewrote the whole library and nothing could round-trip as a .dmp or .fui.
 * Each entry becomes a .gin file here -- the same JSON schema, so nothing is
 * lost -- after which the key is dropped.
 */
std::size_t migrate_legacy_library(const std::filesystem::path &destination) {
  auto stored = read_local_storage(kLegacyKey);
  if (!stored.has_value() || stored->empty()) {
    return 0;
  }

  std::size_t migrated = 0;
  try {
    const auto json = nlohmann::json::parse(*stored);
    if (!json.is_array()) {
      remove_local_storage(kLegacyKey);
      return 0;
    }

    for (const auto &item : json) {
      if (!item.contains("patch")) {
        continue;
      }
      auto patch = item["patch"].get<ym2612::Patch>();
      std::string name = item.value("name", std::string{});
      if (name.empty()) {
        name = item.value("id", std::string{"patch"});
      }
      if (patch.name.empty()) {
        patch.name = name;
      }

      auto filename = patches::sanitize_filename(name);
      if (filename.empty()) {
        filename = "patch";
      }

      auto target = destination / (filename + ".gin");
      for (int suffix = 2; std::filesystem::exists(target); ++suffix) {
        target = destination /
                 (filename + " (" + std::to_string(suffix) + ").gin");
      }

      if (formats::adapter::write_file(ym2612_format::Format::Gin, patch,
                                       target)) {
        ++migrated;
      }
    }
  } catch (const std::exception &error) {
    std::cerr << "megatoy: could not migrate saved patches: " << error.what()
              << std::endl;
    // Leave the key in place so a future version can try again rather than
    // destroying the only copy.
    return migrated;
  }

  remove_local_storage(kLegacyKey);
  if (migrated > 0) {
    std::cout << "megatoy: migrated " << migrated
              << " patch(es) out of localStorage into " << destination
              << std::endl;
  }
  return migrated;
}

} // namespace

bool bootstrap_workspace(megatoy::workspace::Workspace &workspace,
                         const std::filesystem::path &storage_root) {
  if (storage_root.empty()) {
    return false;
  }

  std::error_code ec;
  std::filesystem::create_directories(storage_root, ec);
  if (ec) {
    std::cerr << "megatoy: persistent storage unavailable: " << ec.message()
              << std::endl;
    return false;
  }

  const auto home = storage_root / kDefaultFolderName;
  const bool existed = std::filesystem::is_directory(home, ec);
  if (!existed) {
    std::filesystem::create_directories(home, ec);
    if (ec) {
      std::cerr << "megatoy: could not create " << home << ": " << ec.message()
                << std::endl;
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
  for (const auto &entry :
       std::filesystem::directory_iterator(storage_root, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_directory() || workspace.contains(entry.path())) {
      continue;
    }
    if (workspace.add(entry.path())) {
      changed = true;
    }
  }

  return changed || migrated > 0;
}

} // namespace platform::web

#endif
