#include "../test_check.hpp"
#include "patches/folder_metadata.hpp"
#include "patches/patch_repository.hpp"
#include "patches/patch_write.hpp"
#include "platform/std_file_system.hpp"
#include "workspace/workspace.hpp"

#include <filesystem>
#include <iostream>

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

void test_writable_storage_deletes_patch(const fs::path &root) {
  platform::StdFileSystem file_system;
  const auto writable = root / "writable";
  fs::create_directories(writable);

  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(writable));
  patches::PatchRepository repository(file_system, workspace);

  ym2612::Patch patch;
  patch.name = "delete me";
  const auto saved = repository.save_patch(patch, patch.name,
                                           /*overwrite=*/true, ".gin");
  CHECK(saved.status == patches::SavePatchResult::Status::Success);
  CHECK(fs::exists(saved.path));

  const auto relative =
      repository.to_relative_path(saved.path).generic_string();
  patches::PatchMetadata metadata;
  metadata.star_rating = 5;
  CHECK(repository.save_patch_metadata(relative, patch, metadata));

  const auto *found = find_entry(repository.tree(), saved.path);
  CHECK(found != nullptr);
  const auto entry = *found;
  CHECK(repository.can_edit_metadata(entry));
  CHECK(repository.can_delete_patch(entry));
  CHECK(repository.delete_patch(entry));
  CHECK(!fs::exists(saved.path));
  CHECK(find_entry(repository.tree(), saved.path) == nullptr);

  patches::FolderMetadataStore metadata_store(writable / ".megatoy" /
                                              "patches.json");
  CHECK(metadata_store.load());
  CHECK(
      !metadata_store.get(saved.path.filename().generic_string()).has_value());
}

void test_read_only_storage_refuses_delete(const fs::path &root) {
  platform::StdFileSystem file_system;
  const auto builtin = root / "builtin";
  fs::create_directories(builtin);

  ym2612::Patch patch;
  patch.name = "preset";
  const auto path = builtin / "preset.gin";
  CHECK(patches::write_patch(patch, path));

  megatoy::workspace::Workspace workspace;
  patches::PatchRepository repository(file_system, workspace, builtin);
  const auto *found = find_entry(repository.tree(), path);
  CHECK(found != nullptr);
  const auto entry = *found;
  CHECK(!repository.can_edit_metadata(entry));
  CHECK(!repository.can_delete_patch(entry));
  CHECK(!repository.delete_patch(entry));
  CHECK(fs::exists(path));
}

} // namespace

int main() {
  const auto root =
      fs::temp_directory_path() / "megatoy_patch_repository_delete_test";
  fs::remove_all(root);
  fs::create_directories(root);

  test_writable_storage_deletes_patch(root);
  test_read_only_storage_refuses_delete(root);

  fs::remove_all(root);
  std::cout << "All patch repository delete tests passed\n";
  return 0;
}
