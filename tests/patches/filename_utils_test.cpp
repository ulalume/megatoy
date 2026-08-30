#include "patches/filename_utils.hpp"

#include "../test_check.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace {

void test_append_extension_if_missing() {
  using patches::append_extension_if_missing;

  CHECK(append_extension_if_missing("Bass 2.0", ".gin") == "Bass 2.0.gin");
  CHECK(append_extension_if_missing("bass.GIN", ".gin") == "bass.GIN");
  CHECK(append_extension_if_missing("bass.gIn", "GIN") == "bass.gIn");
  CHECK(append_extension_if_missing("bass", "") == "bass");
  CHECK(append_extension_if_missing("bass", ".gin") == "bass.gin");
}

void test_new_patch_name_error(const fs::path &root) {
  using patches::new_patch_name_error;

  const auto folder = root / "folder";
  fs::create_directories(folder);

  CHECK(new_patch_name_error("bass", ".gin", folder).empty());

  CHECK(!new_patch_name_error("", ".gin", folder).empty());
  // A name that survives only as leading/trailing padding is no name at all,
  // and is reported as an unusable one rather than as an empty one.
  CHECK(!new_patch_name_error("   ", ".gin", folder).empty());
  CHECK(!new_patch_name_error("...", ".gin", folder).empty());

  // Characters the filesystem will not take, including a path separator that
  // would otherwise write outside the folder.
  CHECK(!new_patch_name_error("bad/name", ".gin", folder).empty());
  CHECK(!new_patch_name_error("bad\\name", ".gin", folder).empty());
  CHECK(!new_patch_name_error("bad:name", ".gin", folder).empty());
  CHECK(!new_patch_name_error("bad\nname", ".gin", folder).empty());
  CHECK(!new_patch_name_error("trailing ", ".gin", folder).empty());

  // The collision depends on the chosen format: the same stem is taken in one
  // and free in another.
  std::ofstream(folder / "taken.gin") << "{}";
  CHECK(!new_patch_name_error("taken", ".gin", folder).empty());
  CHECK(new_patch_name_error("taken", ".dmp", folder).empty());

  // A folder in the way counts as taken, whatever it holds.
  fs::create_directories(folder / "occupied.mml");
  CHECK(!new_patch_name_error("occupied", ".mml", folder).empty());

  // An extension without its dot is the same extension.
  CHECK(!new_patch_name_error("taken", "gin", folder).empty());

  fs::remove_all(folder);
}

} // namespace

int main() {
  const auto root = fs::temp_directory_path() / "megatoy_filename_utils_test";
  fs::remove_all(root);
  fs::create_directories(root);

  test_append_extension_if_missing();
  test_new_patch_name_error(root);

  fs::remove_all(root);
  std::cout << "All filename utility tests passed\n";
  return 0;
}
