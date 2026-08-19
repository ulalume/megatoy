#include "../test_check.hpp"
#include "patches/folder_metadata.hpp"
#include "workspace/path_policy.hpp"
#include "workspace/workspace.hpp"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace {

fs::path make_root() {
  auto root = fs::temp_directory_path() / "megatoy_workspace_test";
  fs::remove_all(root);
  fs::create_directories(root / "alpha");
  fs::create_directories(root / "beta");
  fs::create_directories(root / "alpha" / "nested");
  return fs::weakly_canonical(root);
}

void test_add_remove_and_dedupe(const fs::path &root) {
  megatoy::workspace::Workspace workspace;
  CHECK(workspace.empty());

  CHECK(workspace.add(root / "alpha"));
  CHECK(workspace.add(root / "beta"));
  CHECK(workspace.folders().size() == 2);

  // The same folder reached by a different route is still the same folder.
  CHECK(!workspace.add(root / "alpha"));
  CHECK(!workspace.add(root / "alpha" / "nested" / ".."));
  CHECK(workspace.folders().size() == 2);

  // A file, or something that does not exist, is not a folder.
  CHECK(!workspace.add(root / "does_not_exist"));

  CHECK(workspace.remove(root / "alpha"));
  CHECK(!workspace.remove(root / "alpha"));
  CHECK(workspace.folders().size() == 1);
  CHECK(workspace.folders()[0].name == "beta");
}

void test_order_and_default_save_folder(const fs::path &root) {
  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(root / "alpha"));
  CHECK(workspace.add(root / "beta"));

  // New patches go to the first writable folder.
  CHECK(workspace.default_save_folder() == root / "alpha");

  CHECK(workspace.reorder(1, 0));
  CHECK(workspace.folders()[0].name == "beta");
  CHECK(workspace.default_save_folder() == root / "beta");

  CHECK(!workspace.reorder(0, 0));
  CHECK(!workspace.reorder(0, 5));
}

// A folder on an unplugged drive must not silently vanish from the list.
void test_missing_folder_is_kept(const fs::path &root) {
  const auto temporary = root / "removable";
  fs::create_directories(temporary);

  megatoy::workspace::Workspace workspace;
  workspace.set_paths({root / "alpha", temporary});
  CHECK(workspace.folders().size() == 2);
  CHECK(workspace.folders()[1].available);

  fs::remove_all(temporary);
  workspace.refresh();
  CHECK(workspace.folders().size() == 2);
  CHECK(!workspace.folders()[1].available);
  CHECK(!workspace.folders()[1].writable);
  // A missing folder cannot be the save target.
  CHECK(workspace.default_save_folder() == root / "alpha");

  // Round-tripping through preferences keeps it too.
  megatoy::workspace::Workspace reloaded;
  reloaded.set_paths(workspace.paths());
  CHECK(reloaded.folders().size() == 2);
}

void test_refresh_recovers_folder_availability(const fs::path &root) {
  const auto removable = root / "refresh-removable";
  fs::create_directories(removable);

  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(removable));
  CHECK(workspace.folders()[0].available);
  const auto after_add = workspace.revision();

  fs::remove_all(removable);
  CHECK(workspace.refresh());
  CHECK(!workspace.folders()[0].available);
  CHECK(!workspace.folders()[0].writable);
  CHECK(workspace.revision() > after_add);

  const auto after_removal = workspace.revision();
  CHECK(!workspace.refresh());
  CHECK(workspace.revision() == after_removal);

  fs::create_directories(removable);
  CHECK(workspace.refresh());
  CHECK(workspace.folders()[0].available);
  CHECK(workspace.folders()[0].writable);
  CHECK(workspace.revision() > after_removal);
}

void test_owner_lookup(const fs::path &root) {
  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(root / "alpha"));
  CHECK(workspace.add(root / "alpha" / "nested"));

  // Nested folders are allowed; the deepest match wins.
  const auto *owner = workspace.owner_of(root / "alpha" / "nested" / "x.dmp");
  CHECK(owner != nullptr);
  CHECK(owner->path == root / "alpha" / "nested");

  const auto *shallow = workspace.owner_of(root / "alpha" / "y.dmp");
  CHECK(shallow != nullptr);
  CHECK(shallow->path == root / "alpha");

  CHECK(workspace.owner_of(root / "beta" / "z.dmp") == nullptr);
}

void test_revision_tracks_changes(const fs::path &root) {
  megatoy::workspace::Workspace workspace;
  const auto initial = workspace.revision();
  CHECK(workspace.add(root / "alpha"));
  CHECK(workspace.revision() != initial);

  const auto after_add = workspace.revision();
  CHECK(!workspace.add(root / "alpha"));
  // A rejected add must not make observers rebuild for nothing.
  CHECK(workspace.revision() == after_add);
}

void test_path_comparison_policy(const fs::path &root) {
  const auto alpha = root / "alpha";
  CHECK(megatoy::workspace::paths_equal(alpha, alpha.string() + "/"));
  CHECK(megatoy::workspace::paths_equal(alpha, alpha / "nested" / ".."));
  CHECK(!megatoy::workspace::paths_equal(alpha, root / "beta"));
}

void test_metadata_sidecar(const fs::path &root) {
  const auto sidecar = root / "alpha" / ".megatoy" / "patches.json";

  {
    patches::FolderMetadataStore store(sidecar);
    CHECK(store.load());
    CHECK(store.empty());

    patches::PatchMetadata metadata;
    metadata.path = "leads/bright.dmp";
    metadata.star_rating = 5;
    metadata.category = "lead";
    CHECK(store.put(metadata));
  }

  CHECK(fs::exists(sidecar));

  {
    patches::FolderMetadataStore store(sidecar);
    CHECK(store.load());
    const auto stored = store.get("leads/bright.dmp");
    CHECK(stored.has_value());
    CHECK(stored->star_rating == 5);
    CHECK(stored->category == "lead");
    CHECK(!stored->created_at.empty());

    // Entries for files that no longer exist are dropped.
    CHECK(store.retain_only({"leads/other.dmp"}));
    CHECK(!store.get("leads/bright.dmp").has_value());
  }
}

} // namespace

int main() {
  const auto root = make_root();

  test_add_remove_and_dedupe(root);
  test_order_and_default_save_folder(root);
  test_missing_folder_is_kept(root);
  test_refresh_recovers_folder_availability(root);
  test_owner_lookup(root);
  test_revision_tracks_changes(root);
  test_path_comparison_policy(root);
  test_metadata_sidecar(root);

  fs::remove_all(root);
  std::cout << "All workspace tests passed\n";
  return 0;
}
