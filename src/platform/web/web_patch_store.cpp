#include "platform/web/web_patch_store.hpp"
#include "platform/platform_config.hpp"

#if defined(MEGATOY_PLATFORM_WEB)
#include "patches/filename_utils.hpp"
#include "platform/web/local_storage.hpp"
#include "ym2612/patch.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <optional>

namespace {

const char *kStorageKey = "megatoy_saved_patches";

struct StoredPatch {
  std::string id;
  std::string name;
  ym2612::Patch patch;
};

std::string make_storage_id(const std::string &display_name) {
  auto sanitized = patches::sanitize_filename(display_name);
  if (sanitized.empty()) {
    return "patch";
  }
  return sanitized;
}

void store_all(const std::vector<StoredPatch> &entries) {
  nlohmann::json json = nlohmann::json::array();
  for (const auto &entry : entries) {
    json.push_back(
        {{"id", entry.id}, {"name", entry.name}, {"patch", entry.patch}});
  }
  platform::web::write_local_storage(kStorageKey, json.dump());
}

std::vector<StoredPatch> load_all() {
  std::vector<StoredPatch> entries;
  auto stored = platform::web::read_local_storage(kStorageKey);
  if (!stored.has_value() || stored->empty()) {
    return entries;
  }

  try {
    auto json = nlohmann::json::parse(stored.value());
    if (!json.is_array()) {
      return entries;
    }
    bool needs_migration = false;
    for (const auto &item : json) {
      if (!item.contains("patch")) {
        needs_migration = true;
        continue;
      }
      StoredPatch entry;
      entry.name = item.value("name", std::string{});
      entry.patch = item["patch"].get<ym2612::Patch>();
      std::string stored_id = item.value("id", std::string{});
      std::string sanitized = make_storage_id(
          entry.name.empty() ? stored_id : entry.name);
      if (sanitized.empty()) {
        continue;
      }
      if (stored_id != sanitized) {
        needs_migration = true;
      }
      entry.id = sanitized;
      auto existing =
          std::find_if(entries.begin(), entries.end(),
                       [&](const StoredPatch &value) {
                         return value.id == entry.id;
                       });
      if (existing != entries.end()) {
        *existing = entry;
        needs_migration = true;
      } else {
        entries.push_back(std::move(entry));
      }
    }
    if (needs_migration) {
      store_all(entries);
    }
  } catch (...) {
    platform::web::remove_local_storage(kStorageKey);
  }
  return entries;
}

} // namespace

namespace platform::web::patch_store {

std::vector<SavedPatchInfo> list() {
  std::vector<SavedPatchInfo> result;
  auto entries = load_all();
  std::sort(entries.begin(), entries.end(),
            [](const StoredPatch &a, const StoredPatch &b) {
              return a.name < b.name;
            });
  for (const auto &entry : entries) {
    result.push_back({entry.id, entry.name});
  }
  return result;
}

bool save(const ym2612::Patch &patch, const std::string &display_name) {
  auto entries = load_all();
  std::string name = display_name.empty() ? patch.name : display_name;
  std::string storage_id = make_storage_id(name);
  if (storage_id.empty()) {
    return false;
  }

  bool updated = false;
  for (auto &entry : entries) {
    if (entry.id == storage_id) {
      entry.name = patch.name.empty() ? name : patch.name;
      entry.patch = patch;
      updated = true;
      break;
    }
  }
  if (!updated) {
    StoredPatch entry;
    entry.id = storage_id;
    entry.name = patch.name.empty() ? name : patch.name;
    entry.patch = patch;
    entries.push_back(std::move(entry));
  }

  store_all(entries);
  return true;
}

bool load(const std::string &id, ym2612::Patch &out_patch) {
  auto entries = load_all();
  std::string storage_id = make_storage_id(id);
  if (storage_id.empty()) {
    return false;
  }
  for (const auto &entry : entries) {
    if (entry.id == storage_id) {
      out_patch = entry.patch;
      return true;
    }
  }
  return false;
}

bool remove(const std::string &id) {
  auto entries = load_all();
  std::string storage_id = make_storage_id(id);
  if (storage_id.empty()) {
    return false;
  }
  bool removed = false;
  auto it = entries.begin();
  while (it != entries.end()) {
    if (it->id == storage_id) {
      it = entries.erase(it);
      removed = true;
    } else {
      ++it;
    }
  }
  if (removed) {
    store_all(entries);
  }
  return removed;
}

} // namespace platform::web::patch_store

#else

namespace platform::web::patch_store {

std::vector<SavedPatchInfo> list() { return {}; }
bool save(const ym2612::Patch &, const std::string &) { return false; }
bool load(const std::string &, ym2612::Patch &) { return false; }
bool remove(const std::string &) { return false; }

} // namespace platform::web::patch_store

#endif
