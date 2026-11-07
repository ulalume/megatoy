#include "patches/folder_metadata.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace patches {

namespace {

constexpr int kSchemaVersion = 1;

bool replace_file(const std::filesystem::path &source,
                  const std::filesystem::path &destination,
                  std::error_code &error) {
#if defined(_WIN32)
  if (::MoveFileExW(source.c_str(), destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    error.clear();
    return true;
  }
  error = std::error_code(static_cast<int>(::GetLastError()),
                          std::system_category());
  return false;
#else
  std::filesystem::rename(source, destination, error);
  return !error;
#endif
}

std::string iso8601_utc_now() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm tm = {};
#if defined(_WIN32)
  gmtime_s(&tm, &time);
#else
  gmtime_r(&time, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return oss.str();
}

constexpr std::array<std::string_view, 7> kKnownFields = {
    "hash", "star_rating", "icon", "tags", "notes", "created_at", "updated_at"};

bool is_known_field(std::string_view key) {
  return std::find(kKnownFields.begin(), kKnownFields.end(), key) !=
         kKnownFields.end();
}

bool is_stamp(std::string_view key) {
  return key == "created_at" || key == "updated_at";
}

/// The value a known field has when it is unset.
nlohmann::json empty_value_of(std::string_view key) {
  if (key == "star_rating") {
    return 0;
  }
  if (key == "tags") {
    return nlohmann::json::array();
  }
  return std::string{};
}

PatchMetadata metadata_from_json(const std::string &path,
                                 const nlohmann::json &j) {
  PatchMetadata metadata;
  metadata.path = path;
  if (!j.is_object()) {
    return metadata;
  }
  metadata.hash = j.value("hash", std::string{});
  metadata.star_rating = std::clamp(j.value("star_rating", 0), 0, 5);
  metadata.icon = j.value("icon", std::string{});
  metadata.notes = j.value("notes", std::string{});
  metadata.created_at = j.value("created_at", std::string{});
  metadata.updated_at = j.value("updated_at", std::string{});
  if (j.contains("tags") && j["tags"].is_array()) {
    metadata.tags = j["tags"].get<std::vector<std::string>>();
  }
  for (const auto &[key, value] : j.items()) {
    if (!is_known_field(key)) {
      metadata.extra[key] = value;
    }
  }
  return metadata;
}

nlohmann::json metadata_to_json(const PatchMetadata &metadata) {
  nlohmann::json j =
      metadata.extra.is_object() ? metadata.extra : nlohmann::json::object();
  // Only non-default fields are written, so a sidecar stays readable and
  // diffs stay small.
  if (!metadata.hash.empty()) {
    j["hash"] = metadata.hash;
  }
  if (metadata.star_rating != 0) {
    j["star_rating"] = metadata.star_rating;
  }
  if (!metadata.icon.empty()) {
    j["icon"] = metadata.icon;
  }
  if (!metadata.tags.empty()) {
    j["tags"] = metadata.tags;
  }
  if (!metadata.notes.empty()) {
    j["notes"] = metadata.notes;
  }
  if (!metadata.created_at.empty()) {
    j["created_at"] = metadata.created_at;
  }
  if (!metadata.updated_at.empty()) {
    j["updated_at"] = metadata.updated_at;
  }
  return j;
}

/// Parse a sidecar file. A missing file yields an empty object.
std::optional<nlohmann::json> read_sidecar(const std::filesystem::path &path) {
  std::error_code ec;
  if (path.empty() || !std::filesystem::exists(path, ec) || ec) {
    return nlohmann::json::object();
  }
  std::ifstream file(path);
  if (!file) {
    return std::nullopt;
  }
  try {
    nlohmann::json j;
    file >> j;
    if (!j.is_object()) {
      return nlohmann::json::object();
    }
    return j;
  } catch (const std::exception &e) {
    std::cerr << "Failed to read patch metadata at " << path << ": " << e.what()
              << "\n";
    return std::nullopt;
  }
}

nlohmann::json patches_of(const nlohmann::json &root) {
  const auto it = root.find("patches");
  if (it == root.end() || !it->is_object()) {
    return nlohmann::json::object();
  }
  return *it;
}

} // namespace

FolderMetadataStore::FolderMetadataStore(
    std::filesystem::path sidecar_path,
    std::filesystem::path legacy_sidecar_path,
    std::filesystem::path defaults_path)
    : sidecar_path_(std::move(sidecar_path)),
      legacy_sidecar_path_(std::move(legacy_sidecar_path)),
      defaults_path_(std::move(defaults_path)) {}

void FolderMetadataStore::load_defaults() {
  defaults_.clear();
  if (defaults_path_.empty()) {
    return;
  }
  const auto root = read_sidecar(defaults_path_);
  if (!root) {
    return;
  }
  const auto patches = patches_of(*root);
  for (const auto &[path, value] : patches.items()) {
    if (value.is_object()) {
      defaults_.emplace(path, value);
    }
  }
}

std::optional<PatchMetadata>
FolderMetadataStore::defaults_entry(const std::string &relative_path) const {
  const auto it = defaults_.find(relative_path);
  if (it == defaults_.end()) {
    return std::nullopt;
  }
  return metadata_from_json(relative_path, it->second);
}

nlohmann::json
FolderMetadataStore::sidecar_entry(const PatchMetadata &metadata) const {
  auto full = metadata_to_json(metadata);
  const auto it = defaults_.find(metadata.path);
  if (it == defaults_.end()) {
    return full;
  }
  const auto &base = it->second;
  nlohmann::json own = nlohmann::json::object();
  for (const auto &[key, value] : full.items()) {
    const auto in_base = base.find(key);
    if (in_base == base.end() || *in_base != value) {
      own[key] = value;
    }
  }
  // An unset field is left out of `full`; where the defaults set it, the
  // empty value is written so that it stays cleared.
  for (const auto &[key, value] : base.items()) {
    if (!full.contains(key) && is_known_field(key) && !is_stamp(key) &&
        value != empty_value_of(key)) {
      own[key] = empty_value_of(key);
    }
  }
  for (const auto &[key, value] : own.items()) {
    if (!is_stamp(key)) {
      return own;
    }
  }
  return nullptr;
}

bool FolderMetadataStore::load() {
  entries_.clear();
  extra_ = nlohmann::json::object();
  legacy_loaded_ = false;
  load_defaults();

  const auto root = read_sidecar(sidecar_path_);
  if (!root) {
    return false;
  }
  auto patches = patches_of(*root);

  std::error_code ec;
  const bool has_legacy = !legacy_sidecar_path_.empty() &&
                          legacy_sidecar_path_ != sidecar_path_ &&
                          std::filesystem::exists(legacy_sidecar_path_, ec);
  if (has_legacy) {
    const auto legacy = read_sidecar(legacy_sidecar_path_);
    if (!legacy) {
      return false;
    }
    const auto legacy_patches = patches_of(*legacy);
    for (const auto &[path, old_entry] : legacy_patches.items()) {
      if (!old_entry.is_object()) {
        continue;
      }
      auto &entry = patches[path];
      if (!entry.is_object()) {
        entry = nlohmann::json::object();
      }
      for (const auto &[key, value] : old_entry.items()) {
        if (key != "category" && !entry.contains(key)) {
          entry[key] = value;
        }
      }
    }
    legacy_loaded_ = true;
  }

  try {
    for (const auto &[path, value] : patches.items()) {
      const auto base = defaults_.find(path);
      if (base == defaults_.end() || !value.is_object()) {
        entries_.emplace(path, metadata_from_json(path, value));
        continue;
      }
      auto merged = base->second;
      for (const auto &[key, field] : value.items()) {
        merged[key] = field;
      }
      entries_.emplace(path, metadata_from_json(path, merged));
    }
    for (const auto &[path, value] : defaults_) {
      entries_.emplace(path, metadata_from_json(path, value));
    }
  } catch (const std::exception &e) {
    std::cerr << "Failed to read patch metadata at " << sidecar_path_ << ": "
              << e.what() << "\n";
    entries_.clear();
    legacy_loaded_ = false;
    return false;
  }

  for (const auto &[key, value] : root->items()) {
    if (key != "patches" && key != "version") {
      extra_[key] = value;
    }
  }
  return true;
}

void FolderMetadataStore::remove_legacy_sidecar() {
  std::error_code ec;
  std::filesystem::remove(legacy_sidecar_path_, ec);
  if (ec) {
    std::cerr << "Failed to remove " << legacy_sidecar_path_ << ": "
              << ec.message() << "\n";
    return;
  }
  legacy_loaded_ = false;
  const auto directory = legacy_sidecar_path_.parent_path();
  if (std::filesystem::is_empty(directory, ec) && !ec) {
    std::filesystem::remove(directory, ec);
  }
}

bool FolderMetadataStore::save() {
  std::error_code ec;
  std::filesystem::create_directories(sidecar_path_.parent_path(), ec);
  if (ec) {
    std::cerr << "Failed to create " << sidecar_path_.parent_path() << ": "
              << ec.message() << "\n";
    return false;
  }

  nlohmann::json patches = nlohmann::json::object();
  for (const auto &[path, metadata] : entries_) {
    auto entry = sidecar_entry(metadata);
    if (!entry.is_null()) {
      patches[path] = std::move(entry);
    }
  }

  nlohmann::json root = extra_;
  root["version"] = kSchemaVersion;
  root["patches"] = std::move(patches);

  // Write to a temporary file first so an interrupted save cannot leave a
  // truncated sidecar behind.
  auto temporary = sidecar_path_;
  temporary += ".tmp";
  {
    std::ofstream file(temporary);
    if (!file) {
      std::cerr << "Failed to write patch metadata to " << temporary << "\n";
      return false;
    }
    file << root.dump(2) << "\n";
    if (!file) {
      return false;
    }
  }

  if (!replace_file(temporary, sidecar_path_, ec)) {
    std::filesystem::remove(temporary);
    std::cerr << "Failed to replace " << sidecar_path_ << ": " << ec.message()
              << "\n";
    return false;
  }
  if (legacy_loaded_) {
    remove_legacy_sidecar();
  }
  return true;
}

std::optional<PatchMetadata>
FolderMetadataStore::get(const std::string &relative_path) const {
  const auto it = entries_.find(relative_path);
  if (it == entries_.end()) {
    return std::nullopt;
  }
  return it->second;
}

bool FolderMetadataStore::put(PatchMetadata metadata) {
  if (metadata.path.empty()) {
    return false;
  }

  const auto now = iso8601_utc_now();
  const auto existing = entries_.find(metadata.path);
  if (existing != entries_.end() && !existing->second.created_at.empty()) {
    metadata.created_at = existing->second.created_at;
  } else if (metadata.created_at.empty()) {
    metadata.created_at = now;
  }
  metadata.updated_at = now;

  entries_[metadata.path] = std::move(metadata);
  return save();
}

std::vector<PatchMetadata> FolderMetadataStore::entries() const {
  std::vector<PatchMetadata> all;
  all.reserve(entries_.size());
  for (const auto &[path, metadata] : entries_) {
    all.push_back(metadata);
  }
  return all;
}

bool FolderMetadataStore::merge_newer(
    const std::vector<PatchMetadata> &metadata, std::size_t &changed) {
  changed = 0;
  for (const auto &entry : metadata) {
    if (entry.path.empty()) {
      continue;
    }
    // ISO 8601 in UTC, so later sorts after; an empty stamp is oldest.
    const auto existing = entries_.find(entry.path);
    if (existing == entries_.end() ||
        existing->second.updated_at < entry.updated_at) {
      entries_[entry.path] = entry;
      ++changed;
    }
  }
  if (changed == 0) {
    return true;
  }
  if (save()) {
    return true;
  }
  load();
  return false;
}

bool FolderMetadataStore::remove(const std::string &relative_path) {
  const auto it = entries_.find(relative_path);
  if (it == entries_.end()) {
    return false;
  }
  if (auto base = defaults_entry(relative_path)) {
    it->second = std::move(*base);
  } else {
    entries_.erase(it);
  }
  return save();
}

bool FolderMetadataStore::rename_key_prefix(
    const std::string &old_relative_path,
    const std::string &new_relative_path) {
  if (old_relative_path.empty() || new_relative_path.empty()) {
    return false;
  }
  if (old_relative_path == new_relative_path) {
    return true;
  }

  const std::string old_prefix = old_relative_path + "/";
  std::vector<std::pair<std::string, PatchMetadata>> moved;
  for (const auto &[key, metadata] : entries_) {
    if (key == old_relative_path || key.rfind(old_prefix, 0) == 0) {
      const std::string suffix = key.substr(old_relative_path.size());
      auto renamed = metadata;
      renamed.path = new_relative_path + suffix;
      moved.emplace_back(key, std::move(renamed));
    }
  }
  if (moved.empty()) {
    return true;
  }

  const auto previous = entries_;
  for (const auto &[old_key, metadata] : moved) {
    entries_.erase(old_key);
  }
  for (auto &[old_key, metadata] : moved) {
    entries_.insert_or_assign(metadata.path, std::move(metadata));
  }
  for (const auto &[old_key, metadata] : moved) {
    if (auto base = defaults_entry(old_key)) {
      entries_.emplace(old_key, std::move(*base));
    }
  }
  if (save()) {
    return true;
  }
  entries_ = previous;
  return false;
}

bool FolderMetadataStore::retain_only(
    const std::vector<std::string> &existing_paths) {
  std::vector<std::string> sorted = existing_paths;
  std::sort(sorted.begin(), sorted.end());

  bool changed = false;
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (std::binary_search(sorted.begin(), sorted.end(), it->first)) {
      ++it;
    } else {
      changed = changed || !sidecar_entry(it->second).is_null();
      it = entries_.erase(it);
    }
  }

  if (!changed) {
    return true;
  }
  return save();
}

} // namespace patches
