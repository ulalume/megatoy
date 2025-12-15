#include "platform/platform_config.hpp"

#if defined(MEGATOY_PLATFORM_WEB)
#include "patches/filename_utils.hpp"
#include "patches/web_patch_storage.hpp"
#include "platform/web/web_patch_store.hpp"
#include <algorithm>
#include <filesystem>

namespace {
constexpr std::string_view kLocalStorageRelativeRoot = "localStorage";
constexpr std::string_view kLocalStorageAbsolutePrefix = "localStorage://";
constexpr std::string_view kLocalStorageRelativePrefix = "localStorage/";

std::string extract_local_storage_id(const patches::PatchEntry &entry) {
  const std::string full = entry.full_path.empty()
                               ? std::string{}
                               : entry.full_path.generic_string();
  if (!full.empty() && full.rfind(kLocalStorageAbsolutePrefix, 0) == 0) {
    return full.substr(kLocalStorageAbsolutePrefix.size());
  }
  if (entry.relative_path.rfind(kLocalStorageRelativePrefix, 0) == 0) {
    return entry.relative_path.substr(kLocalStorageRelativePrefix.size());
  }
  return full;
}
} // namespace

namespace patches {

void WebPatchStorage::append_entries(std::vector<PatchEntry> &tree) const {
  std::vector<PatchEntry> local_storage_children;
  for (const auto &info : platform::web::patch_store::list()) {
    PatchEntry entry;
    entry.name = info.name;
    entry.relative_path = std::string(kLocalStorageRelativePrefix) + info.name;
    entry.full_path = std::filesystem::path(
        std::string(kLocalStorageAbsolutePrefix) + info.id);
    entry.format = "web_gin";
    entry.is_directory = false;
    local_storage_children.push_back(std::move(entry));
  }
  if (local_storage_children.empty()) {
    return;
  }
  std::sort(
      local_storage_children.begin(), local_storage_children.end(),
      [](const PatchEntry &a, const PatchEntry &b) { return a.name < b.name; });

  PatchEntry storage_root;
  storage_root.name = std::string(kLocalStorageRelativeRoot);
  storage_root.relative_path = std::string(kLocalStorageRelativeRoot);
  storage_root.full_path =
      std::filesystem::path(std::string(kLocalStorageRelativeRoot));
  storage_root.is_directory = true;
  storage_root.children = std::move(local_storage_children);
  tree.push_back(std::move(storage_root));
}

bool WebPatchStorage::load_patch(const PatchEntry &entry,
                                 ym2612::Patch &out_patch) const {
  if (entry.is_directory || entry.format != "web_gin") {
    return false;
  }
  const std::string id = extract_local_storage_id(entry);
  if (id.empty()) {
    return false;
  }
  return platform::web::patch_store::load(id, out_patch);
}

bool WebPatchStorage::remove_patch(const PatchEntry &entry) {
  if (entry.is_directory || entry.format != "web_gin") {
    return false;
  }
  const std::string id = extract_local_storage_id(entry);
  if (id.empty()) {
    return false;
  }
  return platform::web::patch_store::remove(id);
}

SavePatchResult WebPatchStorage::save_patch(const ym2612::Patch &patch,
                                            const std::string &name,
                                            bool overwrite) {
  const std::string sanitized =
      patches::sanitize_filename(name.empty() ? "patch" : name);
  if (!overwrite && platform::web::patch_store::exists(sanitized)) {
    return SavePatchResult::duplicate();
  }
  if (platform::web::patch_store::save(patch, sanitized)) {
    return SavePatchResult::success(
        std::filesystem::path(std::string(kLocalStorageRelativePrefix) +
                              sanitized));
  }
  return SavePatchResult::error("Failed to save patch in browser storage");
}

bool WebPatchStorage::update_patch_metadata(const std::string &relative_path,
                                            const PatchMetadata &metadata) {
  (void)relative_path;
  (void)metadata;
  return false;
}

std::optional<std::filesystem::path>
WebPatchStorage::to_relative_path(const std::filesystem::path &path) const {
  const std::string generic = path.generic_string();
  if (!generic.empty() && generic.rfind(kLocalStorageRelativeRoot, 0) == 0) {
    return path;
  }
  return std::nullopt;
}

std::optional<std::filesystem::path>
WebPatchStorage::to_absolute_path(const std::filesystem::path &path) const {
  const std::string generic = path.generic_string();
  if (!generic.empty() && generic.rfind(kLocalStorageRelativeRoot, 0) == 0) {
    return path;
  }
  return std::nullopt;
}

} // namespace patches
#endif // MEGATOY_PLATFORM_WEB
