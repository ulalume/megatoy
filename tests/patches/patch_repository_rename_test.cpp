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

// Renaming a folder is a different operation from renaming a file: the whole
// directory moves, everything under it moves with it, and the sidecar keys for
// its contents have to follow.
void test_directory_rename(const fs::path &root) {
  platform::StdFileSystem file_system;
  const auto added = root / "folders";
  fs::create_directories(added);

  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(added));
  patches::PatchRepository repository(file_system, workspace);

  ym2612::Patch patch;
  patch.instrument.algorithm = 6;
  const auto saved = repository.save_patch(patch, "deep",
                                           /*overwrite=*/true, ".gin");
  CHECK(saved.status == patches::SavePatchResult::Status::Success);
  // Taken from the repository rather than from the path handed to add(): the
  // root it stores is resolved, and on macOS /var resolves to /private/var.
  const auto writable = saved.path.parent_path();

  fs::create_directories(writable / "bass");
  fs::rename(saved.path, writable / "bass" / "deep.gin");
  repository.refresh();

  const auto nested =
      repository.to_relative_path(writable / "bass" / "deep.gin")
          .generic_string();
  patches::PatchMetadata metadata;
  metadata.star_rating = 3;
  CHECK(repository.save_patch_metadata(nested, patch, metadata));

  const auto *folder = find_entry(repository.tree(), writable / "bass");
  CHECK(folder != nullptr);
  CHECK(folder->is_directory);

  // A directory can be renamed but not deleted.
  CHECK(repository.can_rename_patch(*folder));
  CHECK(!repository.can_delete_patch(*folder));

  const auto folder_entry = *folder;
  CHECK(repository.rename_patch(folder_entry, "low"));

  CHECK(!fs::exists(writable / "bass"));
  CHECK(fs::exists(writable / "low" / "deep.gin"));

  const auto *moved =
      find_entry(repository.tree(), writable / "low" / "deep.gin");
  CHECK(moved != nullptr);
  ym2612::Patch loaded;
  CHECK(repository.load_patch(*moved, loaded));
  CHECK(loaded.instrument.algorithm == 6);

  patches::FolderMetadataStore metadata_store(writable / ".megatoy" /
                                              "patches.json");
  CHECK(metadata_store.load());
  CHECK(!metadata_store.get("bass/deep.gin").has_value());
  const auto followed = metadata_store.get("low/deep.gin");
  CHECK(followed.has_value());
  CHECK(followed->star_rating == 3);

  // A folder's name is its whole name: the dot is not an extension to keep.
  fs::create_directories(writable / "kit.old");
  fs::copy_file(writable / "low" / "deep.gin", writable / "kit.old" / "a.gin");
  repository.refresh();
  const auto *dotted = find_entry(repository.tree(), writable / "kit.old");
  CHECK(dotted != nullptr);
  const auto dotted_entry = *dotted;
  CHECK(repository.rename_patch(dotted_entry, "kit"));
  CHECK(fs::exists(writable / "kit" / "a.gin"));
  CHECK(!fs::exists(writable / "kit.old"));

  // Renaming onto a folder that is already there is refused.
  fs::create_directories(writable / "taken");
  fs::copy_file(writable / "low" / "deep.gin", writable / "taken" / "b.gin");
  repository.refresh();
  const auto *collide = find_entry(repository.tree(), writable / "low");
  CHECK(collide != nullptr);
  const auto collide_entry = *collide;
  CHECK(!repository.rename_patch(collide_entry, "taken"));
  CHECK(fs::exists(writable / "low" / "deep.gin"));
  CHECK(fs::exists(writable / "taken" / "b.gin"));
}

// The workspace root is a folder too, and the one whose rename the workspace
// list has to be told about.
void test_workspace_root_rename(const fs::path &root) {
  platform::StdFileSystem file_system;
  const auto added = root / "library";
  fs::create_directories(added);

  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(added));
  patches::PatchRepository repository(file_system, workspace);

  ym2612::Patch patch;
  patch.instrument.algorithm = 2;
  const auto saved = repository.save_patch(patch, "lead",
                                           /*overwrite=*/true, ".gin");
  CHECK(saved.status == patches::SavePatchResult::Status::Success);
  const auto original = saved.path.parent_path();

  const auto relative =
      repository.to_relative_path(saved.path).generic_string();
  patches::PatchMetadata metadata;
  metadata.star_rating = 4;
  CHECK(repository.save_patch_metadata(relative, patch, metadata));

  const auto *root_entry = find_entry(repository.tree(), original);
  CHECK(root_entry != nullptr);
  CHECK(repository.can_rename_patch(*root_entry));
  CHECK(!repository.can_delete_patch(*root_entry));

  const auto *stored = workspace.find(original);
  CHECK(stored != nullptr);
  const auto stored_path = stored->path;

  const auto entry = *root_entry;
  CHECK(repository.rename_patch(entry, "sounds"));
  const auto renamed = original.parent_path() / "sounds";
  CHECK(!fs::exists(original));
  CHECK(fs::exists(renamed / "lead.gin"));

  // The sidecar travels inside the folder and its keys are relative to it, so
  // they are still correct without being rewritten.
  patches::FolderMetadataStore metadata_store(renamed / ".megatoy" /
                                              "patches.json");
  CHECK(metadata_store.load());
  const auto kept = metadata_store.get("lead.gin");
  CHECK(kept.has_value());
  CHECK(kept->star_rating == 4);

  // Until the workspace is told, it still points at the old path.
  CHECK(workspace.contains(stored_path));
  CHECK(workspace.rename(stored_path, renamed));
  CHECK(!workspace.contains(stored_path));
  CHECK(workspace.contains(renamed));
  CHECK(workspace.folders().size() == 1);

  CHECK(repository.sync_workspace());
  CHECK(find_entry(repository.tree(), renamed / "lead.gin") != nullptr);
}

void test_workspace_rename_keeps_position_and_refuses_collisions(
    const fs::path &root) {
  const auto first = root / "one";
  const auto second = root / "two";
  const auto third = root / "three";
  fs::create_directories(first);
  fs::create_directories(second);
  fs::create_directories(third);

  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(first));
  CHECK(workspace.add(second));
  CHECK(workspace.add(third));
  const auto revision = workspace.revision();

  const auto *stored = workspace.find(second);
  CHECK(stored != nullptr);
  const auto stored_path = stored->path;

  // Onto a path another folder already occupies: refused, nothing moves.
  CHECK(!workspace.rename(stored_path, third));
  CHECK(workspace.revision() == revision);

  const auto renamed = root / "second";
  fs::rename(second, renamed);
  CHECK(workspace.rename(stored_path, renamed));
  CHECK(workspace.revision() != revision);

  const auto &folders = workspace.folders();
  CHECK(folders.size() == 3);
  CHECK(folders[1].name == "second");
  CHECK(workspace.contains(renamed));
  CHECK(workspace.contains(first));
  CHECK(workspace.contains(third));

  // A path nobody registered is not renamed into the list.
  CHECK(!workspace.rename(root / "never-added", root / "nope"));
}

} // namespace

int main() {
  const auto root =
      fs::temp_directory_path() / "megatoy_patch_repository_rename_test";
  fs::remove_all(root);
  fs::create_directories(root);

  test_repository_rename(root);
  test_directory_rename(root);
  test_workspace_root_rename(root);
  test_workspace_rename_keeps_position_and_refuses_collisions(root);

  fs::remove_all(root);
  std::cout << "All patch repository rename tests passed\n";
  return 0;
}
