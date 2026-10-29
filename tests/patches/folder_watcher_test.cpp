#include "../test_check.hpp"
#include "patches/folder_watcher.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

/// Whether the watcher reports a settled change within `limit`.
bool reports_within(patches::FolderWatcher &watcher,
                    std::chrono::milliseconds limit) {
  const auto end = std::chrono::steady_clock::now() + limit;
  while (std::chrono::steady_clock::now() < end) {
    if (watcher.take_settled_change(50ms)) {
      return true;
    }
    std::this_thread::sleep_for(20ms);
  }
  return false;
}

void write_file(const fs::path &path) {
  std::ofstream(path) << "x";
}

void test_a_change_below_a_watched_folder_is_reported() {
  const auto root = fs::temp_directory_path() / "megatoy_folder_watcher_test";
  fs::remove_all(root);
  fs::create_directories(root / "deep" / "er");

  patches::FolderWatcher watcher;
  watcher.watch({root});
  // Notices of the folders just made can still arrive; they are let go.
  std::this_thread::sleep_for(600ms);
  watcher.take_settled_change(0ms);

  write_file(root / "deep" / "er" / "lead.gin");
  CHECK(reports_within(watcher, 5s));
  CHECK(!watcher.take_settled_change(0ms));

  // Hidden entries, such as Finder's and megatoy's own, are not changes.
  write_file(root / ".DS_Store");
  fs::create_directories(root / ".megatoy");
  write_file(root / ".megatoy" / "patches.json");
  CHECK(!reports_within(watcher, 1500ms));

  fs::remove_all(root);
}

} // namespace

int main() {
  test_a_change_below_a_watched_folder_is_reported();
  std::cout << "All folder watcher tests passed\n";
  return 0;
}
