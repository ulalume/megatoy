#include "formats/ginpkg.hpp"
#include "patches/background_folder_scan.hpp"
#include "patches/filesystem_patch_storage.hpp"
#include "patches/patch_write.hpp"
#include "patches/persistent_parse_cache.hpp"
#include "platform/std_file_system.hpp"
#include "ym2612/patch.hpp"

#include "../test_check.hpp"
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace scan = patches::background_folder_scan;

ym2612::Patch sample_patch(std::string name) {
  ym2612::Patch patch;
  patch.name = std::move(name);
  patch.instrument.algorithm = 3;
  patch.instrument.feedback = 5;
  return patch;
}

struct Library {
  fs::path folder;
  fs::path container;
  std::size_t file_count = 0;

  std::string label() const { return folder.filename().string(); }
};

Library make_library(const fs::path &root, const std::string &name) {
  Library library;
  library.folder = root / name;
  std::error_code error;
  fs::create_directories(library.folder, error);
  CHECK(!error);

  const auto package = formats::ginpkg::save_patch(
      library.folder, sample_patch("Warmed"), "bank");
  CHECK(package.has_value());
  library.container = package->lexically_normal();
  library.file_count = 1;

  // Single-patch files: the walk lists them without opening them, which is
  // what makes them padding for the file counter and nothing else.
  for (const auto *stem : {"lead", "bass"}) {
    CHECK(patches::write_patch(sample_patch(stem),
                               library.folder / (std::string(stem) + ".gin")));
    ++library.file_count;
  }
  return library;
}

void test_warm_fills_cache(const fs::path &root) {
  const auto library = make_library(root, "warm-library");
  const auto label = library.label();

  patches::PersistentParseCache cache;
  cache.load(root / "warm-cache.json");
  CHECK(!cache.dirty());

  scan::ScanProgress progress;
  scan::scan_and_warm(library.folder, label, &cache, progress);

  CHECK(progress.finished.load());
  CHECK(!progress.cancel.load());
  CHECK(progress.files_seen.load() >= library.file_count);
  CHECK(progress.containers_parsed.load() == 1);
  CHECK(cache.dirty());

  std::error_code error;
  const auto file_size = fs::file_size(library.container, error);
  CHECK(!error);
  const auto modified = fs::last_write_time(library.container, error);
  CHECK(!error);

  const auto hit = cache.lookup(library.container, file_size, modified, label);
  CHECK(hit.has_value());
  CHECK(hit->is_directory);
  CHECK(hit->format == "ginpkg");
  CHECK(!hit->children.empty());
  // The label is part of the identity, so a folder that ends up under a
  // uniquified name simply misses and parses again.
  CHECK(!cache.lookup(library.container, file_size, modified, "other")
             .has_value());
}

void test_warm_then_scan_is_parse_free(const fs::path &root) {
  const auto library = make_library(root, "reuse-library");
  const auto label = library.label();

  patches::PersistentParseCache cache;
  cache.load(root / "reuse-cache.json");
  scan::ScanProgress progress;
  scan::scan_and_warm(library.folder, label, &cache, progress);
  CHECK(progress.containers_parsed.load() == 1);

  platform::StdFileSystem file_system;
  patches::FilesystemPatchStorage storage(file_system, library.folder, label,
                                          /*writable=*/true,
                                          /*enable_metadata=*/false, &cache);
  std::vector<patches::PatchEntry> tree;
  storage.append_entries(tree);

  // The whole point: the sync that follows an Add Folder re-walks the
  // directory without parsing a single container.
  CHECK(storage.container_parse_count_for_testing() == 0);
  CHECK(tree.size() == 1);
  CHECK(tree[0].children.size() == library.file_count);
}

void test_cancel_stops_the_walk(const fs::path &root) {
  const auto library = make_library(root, "cancel-library");

  patches::PersistentParseCache cache;
  cache.load(root / "cancel-cache.json");
  scan::ScanProgress progress;
  progress.cancel.store(true);
  scan::scan_and_warm(library.folder, library.label(), &cache, progress);

  CHECK(progress.finished.load());
  CHECK(progress.files_seen.load() <= 1);
  CHECK(progress.containers_parsed.load() == 0);
  CHECK(!cache.dirty());
}

} // namespace

int main() {
  const auto root =
      fs::temp_directory_path() / "megatoy_background_folder_scan_test";
  std::error_code error;
  fs::remove_all(root, error);
  error.clear();
  fs::create_directories(root, error);
  CHECK(!error);

  test_warm_fills_cache(root);
  test_warm_then_scan_is_parse_free(root);
  test_cancel_stops_the_walk(root);

  fs::remove_all(root, error);
  CHECK(!error);
  std::cout << "background_folder_scan_test passed\n";
  return 0;
}
