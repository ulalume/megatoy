#pragma once

#include "patches/patch_repository.hpp"
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace patches {

class PersistentParseCache {
public:
  static constexpr std::size_t kMaxEntries = 2048;

  void load(std::filesystem::path file_path);
  bool save();

  std::optional<PatchEntry> lookup(const std::filesystem::path &absolute_path,
                                   std::uintmax_t file_size,
                                   std::filesystem::file_time_type modified,
                                   std::string_view root_label);
  void store(const std::filesystem::path &absolute_path,
             std::uintmax_t file_size, std::filesystem::file_time_type modified,
             std::string_view root_label, const PatchEntry &subtree);

  bool dirty() const;

private:
  struct Entry {
    std::uintmax_t file_size = 0;
    std::int64_t modified_ms = 0;
    std::string root_label;
    std::int64_t last_used = 0;
    std::uint64_t access_order = 0;
    PatchEntry subtree;
  };

  static std::int64_t
  modified_milliseconds(std::filesystem::file_time_type modified);
  static std::int64_t current_time_seconds();
  void prune_locked();

  mutable std::mutex mutex_;
  std::filesystem::path file_path_;
  std::unordered_map<std::string, Entry> entries_;
  std::uint64_t next_access_order_ = 0;
  bool dirty_ = false;
};

} // namespace patches
