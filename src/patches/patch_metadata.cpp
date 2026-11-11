#include "patches/patch_metadata.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace patches {

namespace fs = std::filesystem;

namespace {

constexpr int kOverlayVersion = 1;
constexpr const char *kLegacyFile = "patches.json";

constexpr std::array<std::string_view, 7> kKnownFields = {
    "hash", "star_rating", "icon", "tags", "notes", "created_at", "updated_at"};

bool is_known_field(std::string_view key) {
  return std::find(kKnownFields.begin(), kKnownFields.end(), key) !=
         kKnownFields.end();
}

bool is_bookkeeping(std::string_view key) {
  return key == "hash" || key == "created_at" || key == "updated_at";
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

bool is_string_array(const nlohmann::json &j) {
  return j.is_array() && std::all_of(j.begin(), j.end(), [](const auto &item) {
           return item.is_string();
         });
}

bool replace_file(const fs::path &source, const fs::path &destination,
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
  fs::rename(source, destination, error);
  return !error;
#endif
}

/// Write `j` through a temporary file, so an interrupted write cannot leave
/// a truncated file behind.
bool write_json_file(const fs::path &path, const nlohmann::json &j) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  if (ec) {
    std::cerr << "Failed to create " << path.parent_path() << ": "
              << ec.message() << "\n";
    return false;
  }
  auto temporary = path;
  temporary += ".tmp";
  {
    std::ofstream file(temporary);
    if (!file) {
      std::cerr << "Failed to write " << temporary << "\n";
      return false;
    }
    file << j.dump(2) << "\n";
    if (!file) {
      return false;
    }
  }
  if (!replace_file(temporary, path, ec)) {
    fs::remove(temporary, ec);
    std::cerr << "Failed to replace " << path << ": " << ec.message() << "\n";
    return false;
  }
  return true;
}

/// Remove `directory` if it is empty, and so on upwards to the sidecar
/// folder it lies in.
void prune(fs::path directory) {
  if (std::find(directory.begin(), directory.end(),
                fs::path(sidecar::kDirectory)) == directory.end()) {
    return;
  }
  std::error_code ec;
  while (fs::is_directory(directory, ec) && fs::is_empty(directory, ec) &&
         !ec) {
    fs::remove(directory, ec);
    if (ec || directory.filename() == sidecar::kDirectory) {
      return;
    }
    directory = directory.parent_path();
  }
}

/// The instruments' sidecar folder of `file`.
fs::path items_path_for(const fs::path &file) {
  return file.parent_path() / sidecar::kDirectory / file.filename();
}

/// Copy the entries of the single-file metadata at `legacy` into sidecars.
/// Returns false when any could not be written.
bool split_legacy(const fs::path &folder, const nlohmann::json &legacy) {
  const auto patches = legacy.find("patches");
  if (patches == legacy.end() || !patches->is_object()) {
    return true;
  }
  bool written = true;
  for (const auto &[relative, old_entry] : patches->items()) {
    const auto normal = fs::path(relative).lexically_normal();
    if (!old_entry.is_object() || normal.empty() || normal.is_absolute() ||
        *normal.begin() == "..") {
      continue;
    }
    const auto path = sidecar::path_for_patch(folder / normal, folder);
    if (path.empty()) {
      continue;
    }
    auto entry = sidecar::read(path);
    if (!entry) {
      continue;
    }
    for (const auto &[key, value] : old_entry.items()) {
      if (key != "category" && !entry->contains(key)) {
        (*entry)[key] = value;
      }
    }
    written = sidecar::write(path, *entry) && written;
  }
  return written;
}

} // namespace

PatchMetadata metadata_from_json(const nlohmann::json &j) {
  PatchMetadata metadata;
  if (!j.is_object()) {
    return metadata;
  }
  for (const auto &[key, value] : j.items()) {
    if (key == "star_rating" && value.is_number_integer() &&
        value.get<int>() >= 0 && value.get<int>() <= 5) {
      metadata.star_rating = value.get<int>();
    } else if (key == "tags" && is_string_array(value)) {
      metadata.tags = value.get<std::vector<std::string>>();
    } else if (key == "hash" && value.is_string()) {
      metadata.hash = value.get<std::string>();
    } else if (key == "icon" && value.is_string()) {
      metadata.icon = value.get<std::string>();
    } else if (key == "notes" && value.is_string()) {
      metadata.notes = value.get<std::string>();
    } else if (key == "created_at" && value.is_string()) {
      metadata.created_at = value.get<std::string>();
    } else if (key == "updated_at" && value.is_string()) {
      metadata.updated_at = value.get<std::string>();
    } else {
      metadata.extra[key] = value;
    }
  }
  return metadata;
}

nlohmann::json metadata_to_json(const PatchMetadata &metadata) {
  nlohmann::json j =
      metadata.extra.is_object() ? metadata.extra : nlohmann::json::object();
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

bool holds_nothing_of_note(const nlohmann::json &j) {
  if (!j.is_object()) {
    return true;
  }
  for (const auto &[key, value] : j.items()) {
    if (!is_bookkeeping(key)) {
      return false;
    }
  }
  return true;
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

namespace sidecar {

fs::path path_for(const fs::path &file, const std::string &item) {
  if (item.empty()) {
    return file.parent_path() / kDirectory /
           (file.filename().string() + ".json");
  }
  return items_path_for(file) / (item + ".json");
}

fs::path path_for_patch(const fs::path &path, const fs::path &root) {
  std::error_code ec;
  fs::path item;
  for (auto current = path;; current = current.parent_path()) {
    if (fs::is_regular_file(current, ec)) {
      return path_for(current, item.generic_string());
    }
    if (current == root || current == current.parent_path()) {
      return {};
    }
    item = item.empty() ? current.filename() : current.filename() / item;
  }
}

std::optional<nlohmann::json> read(const fs::path &sidecar) {
  std::error_code ec;
  if (!fs::exists(sidecar, ec)) {
    return nlohmann::json::object();
  }
  std::ifstream file(sidecar);
  if (!file) {
    return std::nullopt;
  }
  try {
    nlohmann::json j;
    file >> j;
    if (j.is_object()) {
      return j;
    }
  } catch (const std::exception &e) {
    std::cerr << "Failed to read patch metadata at " << sidecar << ": "
              << e.what() << "\n";
    return std::nullopt;
  }
  std::cerr << "Patch metadata at " << sidecar << " is not an object\n";
  return std::nullopt;
}

bool write(const fs::path &sidecar, const nlohmann::json &entry) {
  if (!holds_nothing_of_note(entry)) {
    return write_json_file(sidecar, entry);
  }
  std::error_code ec;
  fs::remove(sidecar, ec);
  if (ec) {
    return false;
  }
  prune(sidecar.parent_path());
  return true;
}

bool move(const fs::path &from, const fs::path &to) {
  const auto from_entry = path_for(from);
  const auto to_entry = path_for(to);
  const auto from_items = items_path_for(from);
  const auto to_items = items_path_for(to);
  std::error_code ec;
  const bool has_entry = fs::exists(from_entry, ec);
  const bool has_items = fs::is_directory(from_items, ec);
  if (!has_entry && !has_items) {
    return true;
  }

  fs::create_directories(to_entry.parent_path(), ec);
  if (ec) {
    return false;
  }
  if (has_entry && !replace_file(from_entry, to_entry, ec)) {
    return false;
  }
  if (has_items) {
    // Whatever is there belongs to no file: `to` did not exist. On a
    // case-insensitive disk it may be `from_items` itself.
    if (fs::exists(to_items, ec) && !fs::equivalent(from_items, to_items, ec)) {
      fs::remove_all(to_items, ec);
    }
    fs::rename(from_items, to_items, ec);
    if (ec) {
      if (has_entry) {
        std::error_code rollback;
        replace_file(to_entry, from_entry, rollback);
      }
      return false;
    }
  }
  prune(from_entry.parent_path());
  return true;
}

bool copy(const fs::path &from, const fs::path &to) {
  bool copied = true;
  const auto put = [&copied](const fs::path &source, const fs::path &target) {
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    copied = !ec &&
             fs::copy_file(source, target, fs::copy_options::overwrite_existing,
                           ec) &&
             !ec && copied;
  };
  std::error_code ec;
  if (fs::is_regular_file(path_for(from), ec)) {
    put(path_for(from), path_for(to));
  }
  const auto items = items_path_for(from);
  for (fs::recursive_directory_iterator it(items, ec), end; !ec && it != end;
       it.increment(ec)) {
    if (it->is_regular_file(ec)) {
      put(it->path(),
          items_path_for(to) / it->path().lexically_relative(items));
    }
  }
  return copied;
}

std::vector<fs::path> files_of(const fs::path &file) {
  std::vector<fs::path> files;
  std::error_code ec;
  if (const auto entry = path_for(file); fs::is_regular_file(entry, ec)) {
    files.push_back(entry);
  }
  for (fs::recursive_directory_iterator it(items_path_for(file), ec), end;
       !ec && it != end; it.increment(ec)) {
    if (it->is_regular_file(ec)) {
      files.push_back(it->path());
    }
  }
  return files;
}

void remove(const fs::path &file) {
  const auto entry = path_for(file);
  std::error_code ec;
  fs::remove(entry, ec);
  fs::remove_all(items_path_for(file), ec);
  prune(entry.parent_path());
}

void migrate_folder(const fs::path &folder) {
  // The newer file first, so its fields win over the older one's.
  const std::array<fs::path, 2> legacy_files = {
      folder / kDirectory / kLegacyFile,
      folder / sidecar::kLegacyDirectory / kLegacyFile};
  for (const auto &legacy : legacy_files) {
    std::error_code ec;
    if (!fs::is_regular_file(legacy, ec)) {
      continue;
    }
    const auto root = read(legacy);
    if (!root || !split_legacy(folder, *root)) {
      continue;
    }
    fs::remove(legacy, ec);
    if (ec) {
      std::cerr << "Failed to remove " << legacy << ": " << ec.message()
                << "\n";
      continue;
    }
    if (fs::is_empty(legacy.parent_path(), ec) && !ec) {
      fs::remove(legacy.parent_path(), ec);
    }
  }
}

} // namespace sidecar

MetadataOverlay::MetadataOverlay(fs::path file) : file_(std::move(file)) {
  if (auto root = sidecar::read(file_)) {
    root_ = std::move(*root);
  }
}

nlohmann::json MetadataOverlay::apply(const std::string &key,
                                      nlohmann::json base) const {
  if (!base.is_object()) {
    base = nlohmann::json::object();
  }
  const auto patches = root_.find("patches");
  if (patches == root_.end() || !patches->is_object()) {
    return base;
  }
  const auto own = patches->find(key);
  if (own == patches->end() || !own->is_object()) {
    return base;
  }
  for (const auto &[field, value] : own->items()) {
    base[field] = value;
  }
  return base;
}

bool MetadataOverlay::put(const std::string &key, const nlohmann::json &entry,
                          const nlohmann::json &base) {
  nlohmann::json own = nlohmann::json::object();
  for (const auto &[field, value] : entry.items()) {
    const auto in_base = base.find(field);
    if (in_base == base.end() || *in_base != value) {
      own[field] = value;
    }
  }
  // An unset field is left out of `entry`; where the base sets it, the
  // empty value is stored so that it stays cleared.
  for (const auto &[field, value] : base.items()) {
    if (!entry.contains(field) && is_known_field(field) &&
        !is_bookkeeping(field) && value != empty_value_of(field)) {
      own[field] = empty_value_of(field);
    }
  }

  auto &patches = root_["patches"];
  if (!patches.is_object()) {
    patches = nlohmann::json::object();
  }
  if (holds_nothing_of_note(own)) {
    patches.erase(key);
  } else {
    patches[key] = std::move(own);
  }
  root_["version"] = kOverlayVersion;
  return write_json_file(file_, root_);
}

} // namespace patches
