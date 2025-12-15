#pragma once

#include "../ym2612/patch.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace formats::ginpkg {

struct HistoryEntry {
  std::string uuid;
  std::string timestamp;
  std::optional<std::string> comment;
};

class GinPackage {
public:
  GinPackage();

  bool Load(const std::filesystem::path &path);
  bool Save(const std::filesystem::path &path) const;

  void Clear();
  bool empty() const;

  void SetCurrentData(std::string data);
  const std::string &current_data() const;
  const std::string &current_timestamp() const { return current_timestamp_; }

  void AddVersion(const std::string &json_snapshot,
                  const std::string &comment = "");

  const std::vector<HistoryEntry> &history() const;
  std::optional<std::string> snapshot(const std::string &uuid) const;
  bool DeleteVersion(const std::string &uuid);

private:
  std::string current_data_;
  std::string current_timestamp_;
  std::vector<HistoryEntry> history_;
  std::unordered_map<std::string, std::string> snapshots_;
};

std::filesystem::path
build_package_path(const std::filesystem::path &patches_dir,
                   const std::string &filename);

const std::optional<std::filesystem::path>
save_patch(const std::filesystem::path &patches_dir, const ym2612::Patch &patch,
           const std::string &filename, const std::string &comment = "");

std::vector<ym2612::Patch> read_file(const std::filesystem::path &package_path);

std::optional<GinPackage> load_package(const std::filesystem::path &path);
std::optional<ym2612::Patch> read_version(const std::filesystem::path &path,
                                          const std::string &uuid);
bool delete_version(const std::filesystem::path &path, const std::string &uuid);

} // namespace formats::ginpkg
