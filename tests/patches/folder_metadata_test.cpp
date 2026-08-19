#include "../test_check.hpp"
#include "patches/folder_metadata.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace {

void test_bare_ginpkg_metadata_gains_latest_twin(const fs::path &root) {
  const auto sidecar = root / ".megatoy" / "patches.json";
  fs::create_directories(sidecar.parent_path());
  {
    std::ofstream file(sidecar);
    file << R"({
  "version": 1,
  "patches": {
    "banks/Favorites.GINPKG": {
      "star_rating": 9,
      "category": "lead"
    }
  }
})";
  }

  patches::FolderMetadataStore store(sidecar);
  CHECK(store.load());
  CHECK(store.get("banks/Favorites.GINPKG").has_value());
  const auto latest = store.get("banks/Favorites.GINPKG/latest");
  CHECK(latest.has_value());
  CHECK(latest->path == "banks/Favorites.GINPKG/latest");
  CHECK(latest->star_rating == 5);
  CHECK(latest->category == "lead");

  patches::FolderMetadataStore reloaded(sidecar);
  CHECK(reloaded.load());
  CHECK(reloaded.get("banks/Favorites.GINPKG/latest").has_value());
}

} // namespace

int main() {
  const auto root = fs::temp_directory_path() / "megatoy_folder_metadata_test";
  fs::remove_all(root);
  fs::create_directories(root);

  test_bare_ginpkg_metadata_gains_latest_twin(root);

  fs::remove_all(root);
  std::cout << "All folder metadata tests passed\n";
  return 0;
}
