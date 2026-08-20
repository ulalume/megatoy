#pragma once

#include "formats/ginpkg.hpp"
#include "ym2612/patch.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace platform::import_pipeline {

inline constexpr std::size_t kConfirmationFileThreshold = 20;
inline constexpr std::uint64_t kConfirmationByteThreshold = 2 * 1024 * 1024;
inline constexpr std::uint64_t kCopyYieldBytes = 256 * 1024;
inline constexpr double kQuotaSafetyFraction = 0.10;
inline constexpr std::uint64_t kQuotaSafetyMinimum = 16 * 1024 * 1024;

struct WarmedContainer {
  std::optional<formats::ginpkg::GinPackage> package;
  std::vector<ym2612::Patch> instruments;
};

struct ValidationResult {
  bool valid = false;
  std::string reason;
  std::shared_ptr<const WarmedContainer> warmed_container;
};

struct ValidationFailure {
  std::filesystem::path relative_path;
  std::string reason;
};

/// The one supported-extension set used by repositories and web imports.
const std::vector<std::string> &supported_extensions();
bool supports_extension(std::string_view extension);
bool needs_confirmation(std::size_t file_count, std::uint64_t byte_count);

/// Perform a real, format-specific validation of a staged file.
ValidationResult validate_file(const std::filesystem::path &path);

/// One-shot session cache populated by validation and consumed by the first
/// post-import repository scan, avoiding a second parse of bank containers.
void store_warmed_container(
    const std::filesystem::path &final_path,
    std::shared_ptr<const WarmedContainer> warmed_container);
std::shared_ptr<const WarmedContainer>
take_warmed_container(const std::filesystem::path &final_path);

/**
 * Transactional staging helper. JavaScript writes beneath staging_root();
 * commit() removes rejected files and atomically renames the remaining tree
 * into final_root(). abort() removes staging and never touches final_root().
 */
class ImportStager {
public:
  ImportStager(std::filesystem::path staging_root,
               std::filesystem::path final_root);
  ~ImportStager();

  ImportStager(const ImportStager &) = delete;
  ImportStager &operator=(const ImportStager &) = delete;

  const std::filesystem::path &staging_root() const { return staging_root_; }
  const std::filesystem::path &final_root() const { return final_root_; }

  bool commit(const std::vector<std::filesystem::path> &validated_files,
              std::string &error);
  bool rollback_commit(std::string &error);
  void abort();
  bool committed() const { return committed_; }

private:
  bool is_safe_relative_path(const std::filesystem::path &path) const;

  std::filesystem::path staging_root_;
  std::filesystem::path final_root_;
  bool committed_ = false;
};

} // namespace platform::import_pipeline
