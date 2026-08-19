#include "../test_check.hpp"
#include "patches/folder_metadata.hpp"
#include "patches/patch_repository.hpp"
#include "platform/std_file_system.hpp"
#include "workspace/workspace.hpp"

#include <filesystem>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

namespace {

const patches::PatchEntry *
find_entry(const std::vector<patches::PatchEntry> &entries,
           const fs::path &full_path) {
  for (const auto &entry : entries) {
    if (entry.full_path == full_path) {
      return &entry;
    }
    if (const auto *found = find_entry(entry.children, full_path)) {
      return found;
    }
  }
  return nullptr;
}

void test_repository_rename(const fs::path &root) {
  platform::StdFileSystem file_system;
  const auto writable = root / "writable";
  fs::create_directories(writable);

  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(writable));
  patches::PatchRepository repository(file_system, workspace);

  ym2612::Patch patch;
  patch.name = "embedded name must be replaced";
  patch.instrument.algorithm = 4;
  const auto gin_roundtrip = repository.save_patch(patch, "roundtrip-stem",
                                                   /*overwrite=*/true, ".gin");
  CHECK(gin_roundtrip.status == patches::SavePatchResult::Status::Success);
  const auto *gin_entry = find_entry(repository.tree(), gin_roundtrip.path);
  CHECK(gin_entry != nullptr);
  ym2612::Patch roundtrip;
  CHECK(repository.load_patch(*gin_entry, roundtrip));
  CHECK(roundtrip.name == "roundtrip-stem");

  const auto saved = repository.save_patch(patch, "old-name",
                                           /*overwrite=*/true, ".ginpkg");
  CHECK(saved.status == patches::SavePatchResult::Status::Success);

  const auto collision = repository.save_patch(patch, "collision",
                                               /*overwrite=*/true, ".ginpkg");
  CHECK(collision.status == patches::SavePatchResult::Status::Success);

  const auto old_relative =
      repository.to_relative_path(saved.path).generic_string();
  patches::PatchMetadata exact_metadata;
  exact_metadata.star_rating = 5;
  CHECK(repository.save_patch_metadata(old_relative, patch, exact_metadata));
  patches::PatchMetadata child_metadata;
  child_metadata.category = "latest";
  CHECK(repository.update_patch_metadata(old_relative + "/latest",
                                         child_metadata));

  const auto *found = find_entry(repository.tree(), saved.path);
  CHECK(found != nullptr);
  const auto old_entry = *found;
  CHECK(repository.rename_patch(old_entry, "renamed"));

  const auto renamed_path =
      old_entry.full_path.parent_path() / "renamed.ginpkg";
  CHECK(!fs::exists(saved.path));
  CHECK(fs::exists(renamed_path));
  CHECK(find_entry(repository.tree(), saved.path) == nullptr);
  const auto *renamed_entry = find_entry(repository.tree(), renamed_path);
  CHECK(renamed_entry != nullptr);
  CHECK(renamed_entry->name == "renamed");
  CHECK(!renamed_entry->children.empty());

  ym2612::Patch loaded;
  CHECK(repository.load_patch(renamed_entry->children.front(), loaded));
  CHECK(loaded.instrument.algorithm == 4);

  patches::FolderMetadataStore metadata_store(writable / ".megatoy" /
                                              "patches.json");
  CHECK(metadata_store.load());
  CHECK(!metadata_store.get("old-name.ginpkg").has_value());
  CHECK(!metadata_store.get("old-name.ginpkg/latest").has_value());
  const auto renamed_exact = metadata_store.get("renamed.ginpkg");
  CHECK(renamed_exact.has_value());
  CHECK(renamed_exact->star_rating == 5);
  const auto renamed_child = metadata_store.get("renamed.ginpkg/latest");
  CHECK(renamed_child.has_value());
  CHECK(renamed_child->category == "latest");

  const auto refreshed_entry = *renamed_entry;
  CHECK(!repository.rename_patch(refreshed_entry, "collision"));
  CHECK(fs::exists(renamed_path));
  CHECK(fs::exists(collision.path));
}

} // namespace

int main() {
  const auto root =
      fs::temp_directory_path() / "megatoy_patch_repository_rename_test";
  fs::remove_all(root);
  fs::create_directories(root);

  test_repository_rename(root);

  fs::remove_all(root);
  std::cout << "All patch repository rename tests passed\n";
  return 0;
}
