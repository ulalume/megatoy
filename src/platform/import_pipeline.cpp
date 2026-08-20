#include "platform/import_pipeline.hpp"

#include "formats/ginpkg.hpp"
#include "formats/ym2612_format_adapter.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <mutex>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace platform::import_pipeline {
namespace {

std::mutex g_warm_cache_mutex;
std::unordered_map<std::filesystem::path,
                   std::shared_ptr<const WarmedContainer>>
    g_warm_cache;

std::string lowercase(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string format_label(const std::string &extension) {
  std::string label = extension;
  if (!label.empty() && label.front() == '.') {
    label.erase(label.begin());
  }
  std::transform(
      label.begin(), label.end(), label.begin(),
      [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return label.empty() ? "instrument" : label;
}

ValidationResult invalid(const std::string &extension,
                         std::string detail = {}) {
  ValidationResult result;
  result.reason = "not a valid " + format_label(extension) + " file";
  if (!detail.empty()) {
    result.reason += " (" + std::move(detail) + ")";
  }
  return result;
}

std::vector<std::uint8_t> read_bytes(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

bool has_prefix(const std::vector<std::uint8_t> &bytes,
                std::initializer_list<std::uint8_t> prefix) {
  return bytes.size() >= prefix.size() &&
         std::equal(prefix.begin(), prefix.end(), bytes.begin());
}

ValidationResult validate_mml(const std::filesystem::path &path,
                              const std::string &extension) {
  const auto bytes = read_bytes(path);
  if (bytes.empty()) {
    return invalid(extension, "empty text");
  }
  std::size_t text_bytes = 0;
  bool has_definition_marker = false;
  for (const auto byte : bytes) {
    if (byte == 0) {
      return invalid(extension, "contains binary data");
    }
    if (byte == '@') {
      has_definition_marker = true;
    }
    if (byte == '\n' || byte == '\r' || byte == '\t' ||
        (byte >= 0x20 && byte != 0x7f) || byte >= 0x80) {
      ++text_bytes;
    }
  }
  if (text_bytes * 100 < bytes.size() * 95) {
    return invalid(extension, "contains binary data");
  }
  if (!has_definition_marker) {
    return invalid(extension, "no instrument definition found");
  }
  return {.valid = true};
}

void remove_empty_directories(const std::filesystem::path &root) {
  std::error_code ec;
  std::vector<std::filesystem::path> directories;
  for (std::filesystem::recursive_directory_iterator it(root, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (it->is_directory(ec) && !ec) {
      directories.push_back(it->path());
    }
  }
  std::sort(directories.begin(), directories.end(),
            [](const auto &left, const auto &right) {
              return left.native().size() > right.native().size();
            });
  for (const auto &directory : directories) {
    std::filesystem::remove(directory, ec);
    ec.clear();
  }
}

} // namespace

const std::vector<std::string> &supported_extensions() {
  static const std::vector<std::string> extensions = [] {
    auto result = formats::adapter::readable_extensions();
    if (std::find(result.begin(), result.end(), ".ginpkg") == result.end()) {
      result.emplace_back(".ginpkg");
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
  }();
  return extensions;
}

bool supports_extension(std::string_view extension) {
  auto normalized = lowercase(std::string(extension));
  if (!normalized.empty() && normalized.front() != '.') {
    normalized.insert(normalized.begin(), '.');
  }
  const auto &extensions = supported_extensions();
  return std::binary_search(extensions.begin(), extensions.end(), normalized);
}

bool needs_confirmation(std::size_t file_count, std::uint64_t byte_count) {
  return file_count > kConfirmationFileThreshold ||
         byte_count > kConfirmationByteThreshold;
}

ValidationResult validate_file(const std::filesystem::path &path) {
  const auto extension = lowercase(path.extension().string());
  if (!supports_extension(extension)) {
    return {.reason = "unsupported file type"};
  }

  if (extension == ".mml") {
    return validate_mml(path, extension);
  }

  if (extension == ".ginpkg") {
    const auto bytes = read_bytes(path);
    if (!has_prefix(bytes, {'P', 'K', 0x03, 0x04}) &&
        !has_prefix(bytes, {'P', 'K', 0x05, 0x06}) &&
        !has_prefix(bytes, {'P', 'K', 0x07, 0x08})) {
      return invalid(extension, "missing ZIP header");
    }
    auto package = formats::ginpkg::load_package(path);
    if (!package || !formats::ginpkg::read_current(*package)) {
      return invalid(extension);
    }
    for (const auto &entry : package->history()) {
      if (!formats::ginpkg::read_version(*package, entry.uuid)) {
        return invalid(extension, "invalid package history");
      }
    }
    auto warmed = std::make_shared<WarmedContainer>();
    warmed->package = std::move(*package);
    return {.valid = true, .warmed_container = std::move(warmed)};
  }

  if (extension == ".vgm" || extension == ".vgz") {
    const auto bytes = read_bytes(path);
    const bool magic_ok = extension == ".vgm"
                              ? has_prefix(bytes, {'V', 'g', 'm', ' '})
                              : has_prefix(bytes, {0x1f, 0x8b});
    if (!magic_ok) {
      return invalid(extension, extension == ".vgm" ? "missing Vgm header"
                                                    : "missing gzip header");
    }
  }

  const auto format = formats::adapter::format_for_extension(extension);
  if (!format) {
    return invalid(extension);
  }
  auto patches = formats::adapter::read_file(*format, path);
  if (patches.empty()) {
    return invalid(extension);
  }
  if (formats::adapter::is_multi_patch(*format)) {
    auto warmed = std::make_shared<WarmedContainer>();
    warmed->instruments = std::move(patches);
    return {.valid = true, .warmed_container = std::move(warmed)};
  }
  return {.valid = true};
}

void store_warmed_container(
    const std::filesystem::path &final_path,
    std::shared_ptr<const WarmedContainer> warmed_container) {
  if (!warmed_container) {
    return;
  }
  const std::lock_guard<std::mutex> lock(g_warm_cache_mutex);
  g_warm_cache.insert_or_assign(final_path.lexically_normal(),
                                std::move(warmed_container));
}

std::shared_ptr<const WarmedContainer>
take_warmed_container(const std::filesystem::path &final_path) {
  const std::lock_guard<std::mutex> lock(g_warm_cache_mutex);
  const auto found = g_warm_cache.find(final_path.lexically_normal());
  if (found == g_warm_cache.end()) {
    return {};
  }
  auto result = std::move(found->second);
  g_warm_cache.erase(found);
  return result;
}

ImportStager::ImportStager(std::filesystem::path staging_root,
                           std::filesystem::path final_root)
    : staging_root_(std::move(staging_root)),
      final_root_(std::move(final_root)) {}

ImportStager::~ImportStager() {
  if (!committed_) {
    abort();
  }
}

bool ImportStager::is_safe_relative_path(
    const std::filesystem::path &path) const {
  if (path.empty() || path.is_absolute()) {
    return false;
  }
  const auto normalized = path.lexically_normal();
  return normalized != "." && !normalized.empty() &&
         *normalized.begin() != "..";
}

bool ImportStager::commit(
    const std::vector<std::filesystem::path> &validated_files,
    std::string &error) {
  error.clear();
  if (committed_) {
    error = "import was already committed";
    return false;
  }

  std::error_code ec;
  if (!std::filesystem::is_directory(staging_root_, ec) || ec) {
    error = "staging directory is missing";
    abort();
    return false;
  }
  if (std::filesystem::exists(final_root_, ec) || ec) {
    error = "import destination already exists";
    abort();
    return false;
  }

  std::unordered_set<std::filesystem::path> keep;
  for (const auto &relative : validated_files) {
    if (!is_safe_relative_path(relative)) {
      error = "unsafe staged path: " + relative.generic_string();
      abort();
      return false;
    }
    keep.insert(relative.lexically_normal());
  }
  if (keep.empty()) {
    error = "no valid instrument files were found";
    abort();
    return false;
  }

  for (std::filesystem::recursive_directory_iterator it(staging_root_, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec) || ec) {
      continue;
    }
    auto relative = std::filesystem::relative(it->path(), staging_root_, ec);
    if (ec) {
      break;
    }
    if (!keep.contains(relative.lexically_normal())) {
      std::filesystem::remove(it->path(), ec);
      if (ec) {
        break;
      }
    }
  }
  if (ec) {
    error = "failed to prepare staged import: " + ec.message();
    abort();
    return false;
  }
  remove_empty_directories(staging_root_);

  std::filesystem::rename(staging_root_, final_root_, ec);
  if (ec) {
    error = "failed to commit staged import: " + ec.message();
    abort();
    return false;
  }
  committed_ = true;
  return true;
}

bool ImportStager::rollback_commit(std::string &error) {
  error.clear();
  if (!committed_) {
    abort();
    return true;
  }
  std::error_code ec;
  std::filesystem::rename(final_root_, staging_root_, ec);
  if (ec) {
    error = "failed to roll back import: " + ec.message();
    return false;
  }
  committed_ = false;
  abort();
  return true;
}

void ImportStager::abort() {
  if (committed_ || staging_root_.empty()) {
    return;
  }
  std::error_code ignored;
  std::filesystem::remove_all(staging_root_, ignored);
}

} // namespace platform::import_pipeline
