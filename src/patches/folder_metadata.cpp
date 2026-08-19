#include "patches/folder_metadata.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
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

PatchMetadata metadata_from_json(const std::string &path,
                                 const nlohmann::json &j) {
  PatchMetadata metadata;
  metadata.path = path;
  metadata.hash = j.value("hash", std::string{});
  metadata.star_rating = std::clamp(j.value("star_rating", 0), 0, 5);
  metadata.category = j.value("category", std::string{});
  metadata.notes = j.value("notes", std::string{});
  metadata.created_at = j.value("created_at", std::string{});
  metadata.updated_at = j.value("updated_at", std::string{});
  if (j.contains("tags") && j["tags"].is_array()) {
    metadata.tags = j["tags"].get<std::vector<std::string>>();
  }
  return metadata;
}

nlohmann::json metadata_to_json(const PatchMetadata &metadata) {
  nlohmann::json j;
  // Only non-default fields are written, so a sidecar stays readable and
  // diffs stay small.
  if (!metadata.hash.empty()) {
    j["hash"] = metadata.hash;
  }
  if (metadata.star_rating != 0) {
    j["star_rating"] = metadata.star_rating;
  }
  if (!metadata.category.empty()) {
    j["category"] = metadata.category;
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

} // namespace

FolderMetadataStore::FolderMetadataStore(std::filesystem::path sidecar_path)
    : sidecar_path_(std::move(sidecar_path)) {}

bool FolderMetadataStore::load() {
  entries_.clear();

  std::error_code ec;
  if (!std::filesystem::exists(sidecar_path_, ec) || ec) {
    return true;
  }

  std::ifstream file(sidecar_path_);
  if (!file) {
    return false;
  }

  try {
    nlohmann::json j;
    file >> j;
    if (!j.contains("patches") || !j["patches"].is_object()) {
      return true;
    }
    for (const auto &[path, value] : j["patches"].items()) {
      entries_.emplace(path, metadata_from_json(path, value));
    }

    std::vector<PatchMetadata> normalized;
    for (const auto &[path, metadata] : entries_) {
      std::string lower_path = path;
      std::transform(
          lower_path.begin(), lower_path.end(), lower_path.begin(),
          [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
      if (!lower_path.ends_with(".ginpkg")) {
        continue;
      }

      const std::string latest_path = path + "/latest";
      if (!entries_.contains(latest_path)) {
        auto latest = metadata;
        latest.path = latest_path;
        normalized.push_back(std::move(latest));
      }
    }
    for (auto &metadata : normalized) {
      const std::string path = metadata.path;
      entries_.emplace(path, std::move(metadata));
    }
    if (!normalized.empty() && !save()) {
      return false;
    }
  } catch (const std::exception &e) {
    std::cerr << "Failed to read patch metadata at " << sidecar_path_ << ": "
              << e.what() << "\n";
    entries_.clear();
    return false;
  }
  return true;
}

bool FolderMetadataStore::save() const {
  std::error_code ec;
  std::filesystem::create_directories(sidecar_path_.parent_path(), ec);
  if (ec) {
    std::cerr << "Failed to create " << sidecar_path_.parent_path() << ": "
              << ec.message() << "\n";
    return false;
  }

  nlohmann::json patches = nlohmann::json::object();
  for (const auto &[path, metadata] : entries_) {
    patches[path] = metadata_to_json(metadata);
  }

  nlohmann::json root;
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

bool FolderMetadataStore::merge_missing(
    const std::vector<PatchMetadata> &metadata, std::size_t &inserted) {
  inserted = 0;
  for (const auto &entry : metadata) {
    if (entry.path.empty()) {
      continue;
    }
    if (entries_.emplace(entry.path, entry).second) {
      ++inserted;
    }
  }
  if (inserted == 0) {
    return true;
  }
  if (save()) {
    return true;
  }

  // Leave this object consistent with the on-disk sidecar after a failed
  // write. The migration will retry on the next launch.
  load();
  inserted = 0;
  return false;
}

bool FolderMetadataStore::remove(const std::string &relative_path) {
  if (entries_.erase(relative_path) == 0) {
    return false;
  }
  return save();
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
      it = entries_.erase(it);
      changed = true;
    }
  }

  if (!changed) {
    return true;
  }
  return save();
}

} // namespace patches
