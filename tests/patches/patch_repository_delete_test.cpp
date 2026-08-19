#include "../test_check.hpp"
#include "formats/ginpkg.hpp"
#include "patches/filesystem_patch_storage.hpp"
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

void test_duplicate_save_requires_overwrite(const fs::path &root) {
  platform::StdFileSystem file_system;
  const auto writable = root / "duplicate";
  fs::create_directories(writable);

  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(writable));
  patches::PatchRepository repository(file_system, workspace);

  ym2612::Patch original;
  original.name = "collision";
  original.instrument.algorithm = 1;
  const auto saved = repository.save_patch(original, original.name,
                                           /*overwrite=*/true, ".gin");
  CHECK(saved.status == patches::SavePatchResult::Status::Success);

  ym2612::Patch replacement = original;
  replacement.instrument.algorithm = 6;
  const auto duplicate = repository.save_patch(replacement, replacement.name,
                                               /*overwrite=*/false, ".gin");
  CHECK(duplicate.status == patches::SavePatchResult::Status::Duplicate);

  const auto overwritten = repository.save_patch(replacement, replacement.name,
                                                 /*overwrite=*/true, ".gin");
  CHECK(overwritten.status == patches::SavePatchResult::Status::Success);
  ym2612::Patch loaded;
  const auto *entry = find_entry(repository.tree(), overwritten.path);
  CHECK(entry != nullptr);
  CHECK(repository.load_patch(*entry, loaded));
  CHECK(loaded.instrument.algorithm == 6);
}

void test_container_parse_cache_invalidates_on_change(const fs::path &root) {
  platform::StdFileSystem file_system;
  const auto writable_input = root / "cache";
  fs::create_directories(writable_input);
  const auto writable = fs::weakly_canonical(writable_input);

  ym2612::Patch patch;
  patch.name = "first";
  auto package_path = formats::ginpkg::save_patch(writable, patch, "versions");
  CHECK(package_path.has_value());
  patch.name = "second";
  CHECK(formats::ginpkg::save_patch(writable, patch, "versions").has_value());
  patch.name = "third";
  CHECK(formats::ginpkg::save_patch(writable, patch, "versions").has_value());

  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(writable));
  patches::PatchRepository repository(file_system, workspace);
  const auto *container = find_entry(repository.tree(), *package_path);
  CHECK(container != nullptr);
  CHECK(container->children.size() == 3);
  CHECK(container->children.front().name == "third (Latest)");

  const auto latest_path = container->children.front().relative_path;
  patches::PatchMetadata metadata;
  metadata.star_rating = 4;
  CHECK(repository.update_patch_metadata(latest_path, metadata));

  repository.refresh();
  container = find_entry(repository.tree(), *package_path);
  CHECK(container != nullptr);
  CHECK(container->children.size() == 3);
  CHECK(container->children.front().metadata.has_value());
  CHECK(container->children.front().metadata->star_rating == 4);

  patches::FilesystemPatchStorage storage(file_system, writable, "cache",
                                          /*writable=*/true,
                                          /*enable_metadata=*/false);
  std::vector<patches::PatchEntry> tree;
  storage.append_entries(tree);
  CHECK(storage.container_parse_count_for_testing() == 1);
  tree.clear();
  storage.append_entries(tree);
  CHECK(storage.container_parse_count_for_testing() == 1);

  patch.name = "fourth";
  CHECK(formats::ginpkg::save_patch(writable, patch, "versions").has_value());
  repository.refresh();
  container = find_entry(repository.tree(), *package_path);
  CHECK(container != nullptr);
  CHECK(container->children.size() == 4);
  CHECK(container->children.front().name == "fourth (Latest)");

  tree.clear();
  storage.append_entries(tree);
  CHECK(storage.container_parse_count_for_testing() == 2);
  container = find_entry(tree, *package_path);
  CHECK(container != nullptr);
  CHECK(container->children.size() == 4);
}

void test_repository_revision_tracks_refresh(const fs::path &root) {
  platform::StdFileSystem file_system;
  const auto writable = root / "revision";
  fs::create_directories(writable);

  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(writable));
  patches::PatchRepository repository(file_system, workspace);
  const auto initial = repository.revision();

  CHECK(!repository.sync_workspace());
  CHECK(repository.revision() == initial);
  (void)repository.tree();
  CHECK(repository.revision() == initial);

  repository.refresh();
  CHECK(repository.revision() > initial);
  const auto after_refresh = repository.revision();
  CHECK(repository.revision() == after_refresh);
}

} // namespace

int main() {
  const auto root =
      fs::temp_directory_path() / "megatoy_patch_repository_delete_test";
  fs::remove_all(root);
  fs::create_directories(root);

  test_writable_storage_deletes_patch(root);
  test_read_only_storage_refuses_delete(root);
  test_duplicate_save_requires_overwrite(root);
  test_container_parse_cache_invalidates_on_change(root);
  test_repository_revision_tracks_refresh(root);

  fs::remove_all(root);
  std::cout << "All patch repository delete tests passed\n";
  return 0;
}
