#include "formats/ginpkg.hpp"
#include "patches/filesystem_patch_storage.hpp"
#include "patches/persistent_parse_cache.hpp"
#include "platform/std_file_system.hpp"
#include "ym2612/patch.hpp"

#include "../test_check.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

fs::file_time_type file_time(std::chrono::nanoseconds duration) {
  return fs::file_time_type(
      std::chrono::duration_cast<fs::file_time_type::duration>(duration));
}

patches::PatchEntry
sample_subtree(const fs::path &container_path,
               std::string relative_path = "library/bank.ginpkg") {
  patches::PatchEntry container;
  container.name = "bank";
  container.relative_path = std::move(relative_path);
  container.full_path = container_path;
  container.format = "ginpkg";
  container.is_directory = true;

  patches::PatchEntry child;
  child.name = "Bright Lead";
  child.relative_path = container.relative_path + "/version_abc";
  child.full_path = container_path;
  child.format = "ginpkg";
  child.is_directory = false;
  child.instrument_index = 7;
  child.source_relative_path = container.relative_path;
  child.container_item_id = "abc-123";
  patches::PatchMetadata metadata;
  metadata.star_rating = 5;
  child.metadata = metadata;
  container.children.push_back(std::move(child));
  return container;
}

void write_text(const fs::path &path, const std::string &text) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  CHECK(static_cast<bool>(output));
  output << text;
  CHECK(static_cast<bool>(output));
}

void check_subtree(const patches::PatchEntry &actual,
                   const patches::PatchEntry &expected) {
  CHECK(actual.name == expected.name);
  CHECK(actual.relative_path == expected.relative_path);
  CHECK(actual.full_path == expected.full_path);
  CHECK(actual.format == expected.format);
  CHECK(actual.is_directory == expected.is_directory);
  CHECK(actual.instrument_index == expected.instrument_index);
  CHECK(actual.source_relative_path == expected.source_relative_path);
  CHECK(actual.container_item_id == expected.container_item_id);
  CHECK(actual.children.size() == expected.children.size());
  for (std::size_t index = 0; index < actual.children.size(); ++index) {
    check_subtree(actual.children[index], expected.children[index]);
  }
}

void test_round_trip_and_identity(const fs::path &root) {
  const auto cache_path = root / "round-trip.json";
  const auto container_path = (root / "bank.ginpkg").lexically_normal();
  const auto modified = file_time(std::chrono::milliseconds(123456));
  const auto subtree = sample_subtree(container_path);

  patches::PersistentParseCache cache;
  cache.load(cache_path);
  CHECK(!cache.dirty());
  cache.store(container_path, 912, modified, "library", subtree);
  CHECK(cache.dirty());
  CHECK(cache.save());
  CHECK(!cache.dirty());

  patches::PersistentParseCache loaded;
  loaded.load(cache_path);
  auto hit = loaded.lookup(container_path, 912, modified, "library");
  CHECK(hit.has_value());
  check_subtree(*hit, subtree);
  CHECK(hit->children[0].instrument_index == 7);
  CHECK(hit->children[0].container_item_id == "abc-123");
  CHECK(!hit->children[0].metadata.has_value());

  CHECK(!loaded.lookup(container_path, 913, modified, "library").has_value());
  CHECK(!loaded
             .lookup(container_path, 912,
                     modified + std::chrono::milliseconds(2), "library")
             .has_value());
  CHECK(!loaded.lookup(container_path, 912, modified, "other").has_value());

  CHECK(loaded
            .lookup(container_path, 912,
                    modified + std::chrono::microseconds(999), "library")
            .has_value());
}

void test_invalid_files(const fs::path &root) {
  const auto cache_path = root / "invalid.json";
  const auto container_path = root / "invalid.ginpkg";
  const auto modified = file_time(std::chrono::milliseconds(42));

  write_text(cache_path, "{not valid json");
  patches::PersistentParseCache corrupted;
  corrupted.load(cache_path);
  CHECK(!corrupted.lookup(container_path, 1, modified, "root").has_value());
  CHECK(!corrupted.dirty());

  write_text(cache_path, R"({"version":2,"entries":[]})");
  patches::PersistentParseCache wrong_version;
  wrong_version.load(cache_path);
  CHECK(!wrong_version.lookup(container_path, 1, modified, "root").has_value());
  CHECK(!wrong_version.dirty());
}

void test_cap_eviction(const fs::path &root) {
  const auto cache_path = root / "cap.json";
  const auto modified = file_time(std::chrono::milliseconds(500));
  constexpr std::size_t extra_entries = 5;
  const auto entry_count =
      patches::PersistentParseCache::kMaxEntries + extra_entries;

  patches::PersistentParseCache cache;
  cache.load(cache_path);
  for (std::size_t index = 0; index < entry_count; ++index) {
    const auto path = root / ("container-" + std::to_string(index) + ".ginpkg");
    cache.store(path, index + 1, modified, "cap",
                sample_subtree(path, "cap/" + path.filename().string()));
  }
  CHECK(cache.save());

  patches::PersistentParseCache loaded;
  loaded.load(cache_path);
  for (std::size_t index = 0; index < entry_count; ++index) {
    const auto path = root / ("container-" + std::to_string(index) + ".ginpkg");
    const auto hit = loaded.lookup(path, index + 1, modified, "cap");
    CHECK(hit.has_value() == (index >= extra_entries));
  }
}

ym2612::Patch integration_patch() {
  ym2612::Patch patch;
  patch.name = "Persistent integration";
  patch.instrument.algorithm = 4;
  patch.instrument.feedback = 6;
  return patch;
}

void test_filesystem_storage_integration(const fs::path &root) {
  const auto library = root / "integration-library";
  const auto cache_path = root / "integration-cache.json";
  const auto package =
      formats::ginpkg::save_patch(library, integration_patch(), "cached");
  CHECK(package.has_value());

  platform::StdFileSystem file_system;
  patches::PersistentParseCache first_cache;
  first_cache.load(cache_path);
  patches::FilesystemPatchStorage first_storage(
      file_system, library, "library", /*writable=*/true,
      /*enable_metadata=*/false, &first_cache);
  std::vector<patches::PatchEntry> first_tree;
  first_storage.append_entries(first_tree);
  CHECK(first_storage.container_parse_count_for_testing() == 1);
  CHECK(first_tree.size() == 1);
  CHECK(first_tree[0].children.size() == 1);
  CHECK(first_cache.save());

  patches::PersistentParseCache second_cache;
  second_cache.load(cache_path);
  patches::FilesystemPatchStorage second_storage(
      file_system, library, "library", /*writable=*/true,
      /*enable_metadata=*/false, &second_cache);
  std::vector<patches::PatchEntry> second_tree;
  second_storage.append_entries(second_tree);
  CHECK(second_storage.container_parse_count_for_testing() == 0);
  CHECK(second_tree.size() == first_tree.size());
  check_subtree(second_tree[0], first_tree[0]);
}

} // namespace

int main() {
  const auto root =
      fs::temp_directory_path() / "megatoy_persistent_parse_cache_test";
  std::error_code error;
  fs::remove_all(root, error);
  error.clear();
  fs::create_directories(root, error);
  CHECK(!error);

  test_round_trip_and_identity(root);
  test_invalid_files(root);
  test_cap_eviction(root);
  test_filesystem_storage_integration(root);

  fs::remove_all(root, error);
  CHECK(!error);
  std::cout << "persistent_parse_cache_test passed\n";
  return 0;
}
