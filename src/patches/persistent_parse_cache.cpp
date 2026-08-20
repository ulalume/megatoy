#include "patches/persistent_parse_cache.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <system_error>
#include <vector>

namespace patches {

namespace {

constexpr int kSchemaVersion = 1;

nlohmann::json subtree_to_json(const PatchEntry &entry) {
  nlohmann::json children = nlohmann::json::array();
  for (const auto &child : entry.children) {
    children.push_back(subtree_to_json(child));
  }

  return nlohmann::json{
      {"name", entry.name},
      {"relative_path", entry.relative_path},
      {"full_path", entry.full_path.generic_string()},
      {"format", entry.format},
      {"is_directory", entry.is_directory},
      {"instrument_index", entry.instrument_index},
      {"source_relative_path", entry.source_relative_path},
      {"container_item_id", entry.container_item_id},
      {"children", std::move(children)},
  };
}

std::optional<PatchEntry> subtree_from_json(const nlohmann::json &json) {
  if (!json.is_object() || !json.contains("name") ||
      !json.at("name").is_string() || !json.contains("relative_path") ||
      !json.at("relative_path").is_string() || !json.contains("full_path") ||
      !json.at("full_path").is_string() || !json.contains("format") ||
      !json.at("format").is_string() || !json.contains("is_directory") ||
      !json.at("is_directory").is_boolean() ||
      !json.contains("instrument_index") ||
      !json.at("instrument_index").is_number_unsigned() ||
      !json.contains("source_relative_path") ||
      !json.at("source_relative_path").is_string() ||
      !json.contains("container_item_id") ||
      !json.at("container_item_id").is_string() || !json.contains("children") ||
      !json.at("children").is_array()) {
    return std::nullopt;
  }

  PatchEntry entry;
  entry.name = json.at("name").get<std::string>();
  entry.relative_path = json.at("relative_path").get<std::string>();
  entry.full_path = json.at("full_path").get<std::string>();
  entry.format = json.at("format").get<std::string>();
  entry.is_directory = json.at("is_directory").get<bool>();
  entry.instrument_index = json.at("instrument_index").get<std::size_t>();
  entry.source_relative_path =
      json.at("source_relative_path").get<std::string>();
  entry.container_item_id = json.at("container_item_id").get<std::string>();
  for (const auto &child_json : json.at("children")) {
    auto child = subtree_from_json(child_json);
    if (!child) {
      return std::nullopt;
    }
    entry.children.push_back(std::move(*child));
  }
  return entry;
}

bool is_signed_integer(const nlohmann::json &json) {
  return json.is_number_integer() || json.is_number_unsigned();
}

} // namespace

void PersistentParseCache::load(std::filesystem::path file_path) {
  std::lock_guard lock(mutex_);
  file_path_ = std::move(file_path);
  entries_.clear();
  next_access_order_ = 0;
  dirty_ = false;

  try {
    std::error_code error;
    if (!std::filesystem::exists(file_path_, error) || error) {
      return;
    }

    std::ifstream input(file_path_, std::ios::binary);
    if (!input) {
      return;
    }
    const std::string text((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    const auto json = nlohmann::json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object() ||
        json.value("version", 0) != kSchemaVersion ||
        !json.contains("entries") || !json.at("entries").is_array()) {
      return;
    }

    for (const auto &item : json.at("entries")) {
      try {
        if (!item.is_object() || !item.contains("path") ||
            !item.at("path").is_string() || !item.contains("size") ||
            !item.at("size").is_number_unsigned() ||
            !item.contains("mtime_ms") ||
            !is_signed_integer(item.at("mtime_ms")) ||
            !item.contains("label") || !item.at("label").is_string() ||
            !item.contains("last_used") ||
            !is_signed_integer(item.at("last_used")) ||
            !item.contains("subtree") || !item.at("subtree").is_object()) {
          continue;
        }

        auto subtree = subtree_from_json(item.at("subtree"));
        if (!subtree) {
          continue;
        }

        Entry entry;
        entry.file_size = item.at("size").get<std::uintmax_t>();
        entry.modified_ms = item.at("mtime_ms").get<std::int64_t>();
        entry.root_label = item.at("label").get<std::string>();
        entry.last_used = item.at("last_used").get<std::int64_t>();
        entry.access_order = next_access_order_++;
        entry.subtree = std::move(*subtree);
        entries_.insert_or_assign(item.at("path").get<std::string>(),
                                  std::move(entry));
      } catch (...) {
        continue;
      }
    }
  } catch (...) {
    entries_.clear();
    next_access_order_ = 0;
    dirty_ = false;
  }
}

bool PersistentParseCache::save() {
  std::lock_guard lock(mutex_);
  if (!dirty_) {
    return true;
  }
  if (file_path_.empty()) {
    return false;
  }

  prune_locked();

  std::vector<std::pair<std::string, const Entry *>> ordered_entries;
  ordered_entries.reserve(entries_.size());
  for (const auto &[path, entry] : entries_) {
    ordered_entries.emplace_back(path, &entry);
  }
  std::sort(ordered_entries.begin(), ordered_entries.end(),
            [](const auto &left, const auto &right) {
              if (left.second->last_used != right.second->last_used) {
                return left.second->last_used < right.second->last_used;
              }
              return left.second->access_order < right.second->access_order;
            });

  nlohmann::json serialized_entries = nlohmann::json::array();
  for (const auto &[path, entry] : ordered_entries) {
    serialized_entries.push_back(
        {{"path", path},
         {"size", entry->file_size},
         {"mtime_ms", entry->modified_ms},
         {"label", entry->root_label},
         {"last_used", entry->last_used},
         {"subtree", subtree_to_json(entry->subtree)}});
  }
  const nlohmann::json root = {{"version", kSchemaVersion},
                               {"entries", std::move(serialized_entries)}};

  std::error_code error;
  if (file_path_.has_parent_path()) {
    std::filesystem::create_directories(file_path_.parent_path(), error);
    if (error) {
      return false;
    }
  }

  auto temporary = file_path_;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      return false;
    }
    output << root.dump() << '\n';
    if (!output) {
      return false;
    }
  }

  std::filesystem::rename(temporary, file_path_, error);
  if (error) {
    std::error_code remove_error;
    std::filesystem::remove(temporary, remove_error);
    return false;
  }

  dirty_ = false;
  return true;
}

std::optional<PatchEntry> PersistentParseCache::lookup(
    const std::filesystem::path &absolute_path, std::uintmax_t file_size,
    std::filesystem::file_time_type modified, std::string_view root_label) {
  std::lock_guard lock(mutex_);
  const auto cached = entries_.find(absolute_path.generic_string());
  if (cached == entries_.end() || cached->second.file_size != file_size ||
      cached->second.modified_ms != modified_milliseconds(modified) ||
      cached->second.root_label != root_label) {
    return std::nullopt;
  }

  cached->second.last_used = current_time_seconds();
  cached->second.access_order = next_access_order_++;
  dirty_ = true;
  return cached->second.subtree;
}

void PersistentParseCache::store(const std::filesystem::path &absolute_path,
                                 std::uintmax_t file_size,
                                 std::filesystem::file_time_type modified,
                                 std::string_view root_label,
                                 const PatchEntry &subtree) {
  std::lock_guard lock(mutex_);
  Entry entry;
  entry.file_size = file_size;
  entry.modified_ms = modified_milliseconds(modified);
  entry.root_label = root_label;
  entry.last_used = current_time_seconds();
  entry.access_order = next_access_order_++;
  entry.subtree = subtree;
  entries_.insert_or_assign(absolute_path.generic_string(), std::move(entry));
  dirty_ = true;
}

bool PersistentParseCache::dirty() const {
  std::lock_guard lock(mutex_);
  return dirty_;
}

std::int64_t PersistentParseCache::modified_milliseconds(
    std::filesystem::file_time_type modified) {
  return std::chrono::floor<std::chrono::milliseconds>(
             modified.time_since_epoch())
      .count();
}

std::int64_t PersistentParseCache::current_time_seconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void PersistentParseCache::prune_locked() {
  if (entries_.size() <= kMaxEntries) {
    return;
  }

  std::vector<std::pair<std::string, Entry *>> ordered_entries;
  ordered_entries.reserve(entries_.size());
  for (auto &[path, entry] : entries_) {
    ordered_entries.emplace_back(path, &entry);
  }
  std::sort(ordered_entries.begin(), ordered_entries.end(),
            [](const auto &left, const auto &right) {
              if (left.second->last_used != right.second->last_used) {
                return left.second->last_used < right.second->last_used;
              }
              return left.second->access_order < right.second->access_order;
            });

  const auto remove_count = entries_.size() - kMaxEntries;
  for (std::size_t index = 0; index < remove_count; ++index) {
    entries_.erase(ordered_entries[index].first);
  }
}

} // namespace patches
