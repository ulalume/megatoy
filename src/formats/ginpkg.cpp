#include "ginpkg.hpp"

#include <miniz.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <system_error>

namespace {

constexpr const char *kCurrentFile = "current.gin";
constexpr const char *kHistoryFile = "history.json";

std::string to_native_string(const std::filesystem::path &path) {
#if defined(_WIN32)
  auto u8 = path.u8string();
  return std::string(u8.begin(), u8.end());
#else
  return path.string();
#endif
}

std::string iso8601_utc_timestamp() {
  using clock = std::chrono::system_clock;
  const auto now = clock::now();
  const std::time_t time = clock::to_time_t(now);
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

std::string generate_uuid() {
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<uint64_t> dist;
  std::array<uint8_t, 16> data{};
  for (size_t i = 0; i < data.size(); i += 8) {
    uint64_t value = dist(gen);
    for (size_t shift = 0; shift < 8; ++shift) {
      const size_t index = i + shift;
      if (index >= data.size()) {
        break;
      }
      data[index] = static_cast<uint8_t>((value >> (shift * 8)) & 0xFF);
    }
  }
  data[6] = static_cast<uint8_t>((data[6] & 0x0F) | 0x40);
  data[8] = static_cast<uint8_t>((data[8] & 0x3F) | 0x80);
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (uint8_t byte : data) {
    oss << std::setw(2) << static_cast<int>(byte);
  }
  return oss.str();
}

std::optional<std::string> read_zip_entry(mz_zip_archive &archive,
                                          const std::string &name) {
  size_t size = 0;
  char *buffer =
      static_cast<char *>(mz_zip_reader_extract_file_to_heap(
          &archive, name.c_str(), &size, 0));
  if (!buffer) {
    return std::nullopt;
  }
  std::string contents(buffer, buffer + size);
  mz_free(buffer);
  return contents;
}

bool write_zip_entry(mz_zip_archive &archive, const std::string &name,
                     const std::string &content) {
  return mz_zip_writer_add_mem_ex(&archive, name.c_str(), content.data(),
                                  content.size(), nullptr, 0,
                                  MZ_NO_COMPRESSION, 0, 0) != 0;
}

nlohmann::json history_to_json(
    const std::vector<formats::ginpkg::HistoryEntry> &history) {
  nlohmann::json versions = nlohmann::json::array();
  for (const auto &entry : history) {
    nlohmann::json node = {{"uuid", entry.uuid},
                           {"timestamp", entry.timestamp}};
    if (entry.comment && !entry.comment->empty()) {
      node["comment"] = *entry.comment;
    }
    versions.push_back(std::move(node));
  }
  return nlohmann::json{{"versions", versions}};
}

} // namespace

namespace formats::ginpkg {

GinPackage::GinPackage() { Clear(); }

void GinPackage::Clear() {
  current_data_.clear();
  history_.clear();
  snapshots_.clear();
}

bool GinPackage::empty() const { return current_data_.empty(); }

void GinPackage::SetCurrentData(std::string data) {
  current_data_ = std::move(data);
}

const std::string &GinPackage::current_data() const { return current_data_; }

const std::vector<HistoryEntry> &GinPackage::history() const {
  return history_;
}

std::optional<std::string> GinPackage::snapshot(const std::string &uuid) const {
  auto it = snapshots_.find(uuid);
  if (it != snapshots_.end()) {
    return it->second;
  }
  return std::nullopt;
}

bool GinPackage::DeleteVersion(const std::string &uuid) {
  const auto before_size = history_.size();
  history_.erase(
      std::remove_if(history_.begin(), history_.end(),
                     [&](const HistoryEntry &entry) { return entry.uuid == uuid; }),
      history_.end());
  snapshots_.erase(uuid);
  return history_.size() != before_size;
}

void GinPackage::AddVersion(const std::string &json_snapshot,
                            const std::string &comment) {
  if (json_snapshot.empty()) {
    return;
  }
  HistoryEntry entry;
  entry.uuid = generate_uuid();
  entry.timestamp = iso8601_utc_timestamp();
  if (!comment.empty()) {
    entry.comment = comment;
  }
  history_.push_back(entry);
  snapshots_[entry.uuid] = json_snapshot;
}

bool GinPackage::Load(const std::filesystem::path &path) {
  Clear();
  if (!std::filesystem::exists(path)) {
    std::cerr << "ginpkg file not found: " << path << std::endl;
    return false;
  }

  mz_zip_archive archive;
  std::memset(&archive, 0, sizeof(archive));
  const std::string native_path = to_native_string(path);
  if (!mz_zip_reader_init_file(&archive, native_path.c_str(), 0)) {
    std::cerr << "Failed to open ginpkg: " << path << std::endl;
    return false;
  }
  struct ReaderGuard {
    mz_zip_archive *archive;
    explicit ReaderGuard(mz_zip_archive *arc) : archive(arc) {}
    ~ReaderGuard() {
      if (archive) {
        mz_zip_reader_end(archive);
      }
    }
  } guard(&archive);

  auto current = read_zip_entry(archive, kCurrentFile);
  if (!current) {
    std::cerr << "ginpkg missing " << kCurrentFile << ": " << path
              << std::endl;
    return false;
  }

  current_data_ = std::move(*current);

  auto history_doc = read_zip_entry(archive, kHistoryFile);
  history_.clear();
  snapshots_.clear();

  if (history_doc) {
    try {
      nlohmann::json j = nlohmann::json::parse(*history_doc);
      if (j.contains("versions") && j["versions"].is_array()) {
        for (const auto &version : j["versions"]) {
          HistoryEntry entry;
          if (version.contains("uuid") && version["uuid"].is_string()) {
            entry.uuid = version["uuid"].get<std::string>();
          }
          if (version.contains("timestamp") &&
              version["timestamp"].is_string()) {
            entry.timestamp = version["timestamp"].get<std::string>();
          }
          if (version.contains("comment") &&
              version["comment"].is_string()) {
            auto value = version["comment"].get<std::string>();
            if (!value.empty()) {
              entry.comment = value;
            }
          }
          if (!entry.uuid.empty() && !entry.timestamp.empty()) {
            history_.push_back(entry);
          }
        }
      }
    } catch (const std::exception &e) {
      std::cerr << "Failed to parse history.json in " << path
                << ": " << e.what() << std::endl;
      history_.clear();
    }
  }

  for (const auto &entry : history_) {
    const std::string snapshot_name = entry.uuid + ".gin";
    auto snapshot = read_zip_entry(archive, snapshot_name);
    if (snapshot) {
      snapshots_[entry.uuid] = std::move(*snapshot);
    } else {
      std::cerr << "Missing snapshot " << snapshot_name << " in " << path
                << std::endl;
    }
  }

  return true;
}

bool GinPackage::Save(const std::filesystem::path &path) const {
  if (current_data_.empty()) {
    std::cerr << "Cannot save ginpkg with empty current data." << std::endl;
    return false;
  }

  auto parent = path.parent_path();
  if (!parent.empty()) {
    std::error_code create_error;
    std::filesystem::create_directories(parent, create_error);
    if (create_error) {
      std::cerr << "Failed to create directory '" << parent
                << "': " << create_error.message() << std::endl;
      return false;
    }
  }

  mz_zip_archive archive;
  std::memset(&archive, 0, sizeof(archive));
  const std::string native_path = to_native_string(path);
  if (!mz_zip_writer_init_file(&archive, native_path.c_str(), 0)) {
    std::cerr << "Failed to open ginpkg for writing: " << path << std::endl;
    return false;
  }

  bool ok = write_zip_entry(archive, kCurrentFile, current_data_);
  if (ok) {
    const auto history_text = history_to_json(history_).dump(2);
    ok = write_zip_entry(archive, kHistoryFile, history_text);
  }
  if (ok) {
    for (const auto &entry : history_) {
      auto it = snapshots_.find(entry.uuid);
      if (it == snapshots_.end()) {
        continue;
      }
      const std::string filename = entry.uuid + ".gin";
      if (!write_zip_entry(archive, filename, it->second)) {
        ok = false;
        break;
      }
    }
  }

  bool finalize_ok = mz_zip_writer_finalize_archive(&archive) != 0;
  bool end_ok = mz_zip_writer_end(&archive) != 0;
  if (!(ok && finalize_ok && end_ok)) {
    std::error_code remove_error;
    std::filesystem::remove(path, remove_error);
    std::cerr << "Failed to finalize ginpkg: " << path << std::endl;
    return false;
  }

  return true;
}

std::filesystem::path
build_package_path(const std::filesystem::path &patches_dir,
                   const std::string &filename) {
  std::string full_filename = filename;
  std::string lower = full_filename;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (!lower.ends_with(".ginpkg")) {
    full_filename += ".ginpkg";
  }
  return patches_dir / full_filename;
}

const std::optional<std::filesystem::path>
save_patch(const std::filesystem::path &patches_dir,
           const ym2612::Patch &patch, const std::string &filename,
           const std::string &comment) {
  try {
    auto package_path = build_package_path(patches_dir, filename);
    GinPackage package;
    if (std::filesystem::exists(package_path)) {
      if (!package.Load(package_path)) {
        return std::nullopt;
      }
      if (!package.current_data().empty()) {
        package.AddVersion(package.current_data(), comment);
      }
    }

    nlohmann::json j = patch;
    package.SetCurrentData(j.dump(2));

    if (!package.Save(package_path)) {
      return std::nullopt;
    }

    return package_path;
  } catch (const std::exception &e) {
    std::cerr << "Failed to save ginpkg: " << e.what() << std::endl;
    return std::nullopt;
  }
}

std::vector<ym2612::Patch> read_file(const std::filesystem::path &package_path) {
  try {
    GinPackage package;
    if (!package.Load(package_path)) {
      return {};
    }

    nlohmann::json j = nlohmann::json::parse(package.current_data());
    ym2612::Patch patch = j.get<ym2612::Patch>();
    return {patch};
  } catch (const std::exception &e) {
    std::cerr << "Failed to load ginpkg '" << package_path
              << "': " << e.what() << std::endl;
    return {};
  }
}

std::optional<GinPackage> load_package(const std::filesystem::path &path) {
  GinPackage package;
  if (!package.Load(path)) {
    return std::nullopt;
  }
  return package;
}

std::optional<ym2612::Patch> read_version(const std::filesystem::path &path,
                                          const std::string &uuid) {
  try {
    auto package = load_package(path);
    if (!package) {
      return std::nullopt;
    }
    auto snapshot = package->snapshot(uuid);
    if (!snapshot) {
      return std::nullopt;
    }
    auto json = nlohmann::json::parse(*snapshot);
    ym2612::Patch patch = json.get<ym2612::Patch>();
    return patch;
  } catch (const std::exception &e) {
    std::cerr << "Failed to load ginpkg version '" << uuid
              << "' from " << path << ": " << e.what() << std::endl;
    return std::nullopt;
  }
}

bool delete_version(const std::filesystem::path &path,
                    const std::string &uuid) {
  try {
    auto package = load_package(path);
    if (!package) {
      return false;
    }
    if (!package->DeleteVersion(uuid)) {
      return false;
    }
    return package->Save(path);
  } catch (const std::exception &e) {
    std::cerr << "Failed to delete ginpkg version '" << uuid
              << "' from " << path << ": " << e.what() << std::endl;
    return false;
  }
}

} // namespace formats::ginpkg
