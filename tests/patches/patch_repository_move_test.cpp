#include "../test_check.hpp"
#include "patches/folder_metadata.hpp"
#include "patches/patch_repository.hpp"
#include "patches/patch_write.hpp"
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

patches::PatchEntry entry_at(const patches::PatchRepository &repository,
                             const fs::path &full_path) {
  const auto *found = find_entry(repository.tree(), full_path);
  CHECK(found != nullptr);
  return *found;
}

ym2612::Patch make_patch(int algorithm) {
  ym2612::Patch patch;
  patch.instrument.algorithm =
      static_cast<decltype(patch.instrument.algorithm)>(algorithm);
  return patch;
}

void test_file_move(const fs::path &root) {
  platform::StdFileSystem file_system;
  const auto folder = root / "files";
  fs::create_directories(folder / "pads");
  CHECK(patches::write_patch(make_patch(1), folder / "pads" / "keep.gin"));
  CHECK(patches::write_patch(make_patch(3), folder / "lead.gin"));

  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(folder));
  patches::PatchRepository repository(file_system, workspace);

  const auto relative =
      repository.to_relative_path(folder / "lead.gin").generic_string();
  patches::PatchMetadata metadata;
  metadata.star_rating = 4;
  metadata.category = "Lead";
  CHECK(repository.update_patch_metadata(relative, metadata));

  const auto file = entry_at(repository, folder / "lead.gin");
  const auto pads = entry_at(repository, folder / "pads");
  const auto root_entry = entry_at(repository, folder);

  // Already where it is: nothing to move.
  CHECK(!repository.can_move_patch(file, root_entry));
  CHECK(repository.can_move_patch(file, pads));
  CHECK(repository.move_patch(file, pads));

  CHECK(!fs::exists(folder / "lead.gin"));
  CHECK(fs::exists(folder / "pads" / "lead.gin"));
  const auto *moved =
      find_entry(repository.tree(), folder / "pads" / "lead.gin");
  CHECK(moved != nullptr);
  CHECK(moved->metadata.has_value());
  CHECK(moved->metadata->star_rating == 4);

  patches::FolderMetadataStore store(folder / ".megatoy" / "patches.json");
  CHECK(store.load());
  CHECK(!store.get("lead.gin").has_value());
  const auto followed = store.get("pads/lead.gin");
  CHECK(followed.has_value());
  CHECK(followed->category == "Lead");

  // And back up into the workspace folder itself.
  const auto back = *moved;
  CHECK(repository.move_patch(back, entry_at(repository, folder)));
  CHECK(fs::exists(folder / "lead.gin"));
  CHECK(!fs::exists(folder / "pads" / "lead.gin"));
}

void test_directory_move(const fs::path &root) {
  platform::StdFileSystem file_system;
  const auto folder = root / "dirs";
  fs::create_directories(folder / "bass" / "inner");
  fs::create_directories(folder / "low");
  CHECK(patches::write_patch(make_patch(6), folder / "bass" / "deep.gin"));
  CHECK(patches::write_patch(make_patch(2),
                             folder / "bass" / "inner" / "sub.gin"));
  CHECK(patches::write_patch(make_patch(1), folder / "low" / "x.gin"));

  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(folder));
  patches::PatchRepository repository(file_system, workspace);

  const auto nested =
      repository.to_relative_path(folder / "bass" / "inner" / "sub.gin")
          .generic_string();
  patches::PatchMetadata metadata;
  metadata.star_rating = 5;
  metadata.category = "Bass";
  CHECK(repository.update_patch_metadata(nested, metadata));

  const auto bass = entry_at(repository, folder / "bass");
  const auto inner = entry_at(repository, folder / "bass" / "inner");
  const auto low = entry_at(repository, folder / "low");

  // Not into itself, nor into anything below it.
  CHECK(!repository.can_move_patch(bass, bass));
  CHECK(!repository.can_move_patch(bass, inner));
  CHECK(!repository.move_patch(bass, inner));

  CHECK(repository.can_move_patch(bass, low));
  CHECK(repository.move_patch(bass, low));
  CHECK(!fs::exists(folder / "bass"));
  CHECK(fs::exists(folder / "low" / "bass" / "deep.gin"));
  CHECK(fs::exists(folder / "low" / "bass" / "inner" / "sub.gin"));

  const auto *moved = find_entry(repository.tree(),
                                 folder / "low" / "bass" / "inner" / "sub.gin");
  CHECK(moved != nullptr);
  CHECK(moved->metadata.has_value());
  CHECK(moved->metadata->star_rating == 5);
  CHECK(moved->metadata->category == "Bass");

  patches::FolderMetadataStore store(folder / ".megatoy" / "patches.json");
  CHECK(store.load());
  CHECK(!store.get("bass/inner/sub.gin").has_value());
  CHECK(store.get("low/bass/inner/sub.gin").has_value());

  // The workspace folder itself is never moved.
  fs::create_directories(folder / "low" / "elsewhere");
  CHECK(patches::write_patch(make_patch(1),
                             folder / "low" / "elsewhere" / "y.gin"));
  repository.refresh();
  CHECK(!repository.can_move_patch(
      entry_at(repository, folder),
      entry_at(repository, folder / "low" / "elsewhere")));
}

void test_collision_refused(const fs::path &root) {
  platform::StdFileSystem file_system;
  const auto folder = root / "collide";
  fs::create_directories(folder / "kit" / "drums");
  fs::create_directories(folder / "drums");
  CHECK(patches::write_patch(make_patch(1), folder / "x.gin"));
  CHECK(patches::write_patch(make_patch(2), folder / "kit" / "x.gin"));
  CHECK(
      patches::write_patch(make_patch(3), folder / "kit" / "drums" / "a.gin"));
  CHECK(patches::write_patch(make_patch(4), folder / "drums" / "b.gin"));

  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(folder));
  patches::PatchRepository repository(file_system, workspace);

  const auto file = entry_at(repository, folder / "x.gin");
  const auto kit = entry_at(repository, folder / "kit");
  CHECK(!repository.move_patch(file, kit));
  CHECK(fs::exists(folder / "x.gin"));
  ym2612::Patch kept;
  CHECK(repository.load_patch(entry_at(repository, folder / "kit" / "x.gin"),
                              kept));
  CHECK(kept.instrument.algorithm == 2);

  const auto drums = entry_at(repository, folder / "drums");
  CHECK(!repository.move_patch(drums, kit));
  CHECK(fs::exists(folder / "drums" / "b.gin"));
  CHECK(fs::exists(folder / "kit" / "drums" / "a.gin"));
  CHECK(!fs::exists(folder / "kit" / "drums" / "b.gin"));
}

void test_containers(const fs::path &root) {
  platform::StdFileSystem file_system;
  const auto folder = root / "containers";
  fs::create_directories(folder / "keep");
  CHECK(patches::write_patch(make_patch(1), folder / "keep" / "x.gin"));

  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(folder));
  patches::PatchRepository repository(file_system, workspace);

  const auto saved = repository.save_patch(make_patch(5), "pack",
                                           /*overwrite=*/true, ".ginpkg");
  CHECK(saved.status == patches::SavePatchResult::Status::Success);
  CHECK(
      repository.save_patch(make_patch(2), "loose", /*overwrite=*/true, ".gin")
          .status == patches::SavePatchResult::Status::Success);
  const auto package_relative =
      repository.to_relative_path(saved.path).generic_string();
  patches::PatchMetadata metadata;
  metadata.star_rating = 2;
  CHECK(
      repository.update_patch_metadata(package_relative + "/latest", metadata));

  const auto package = entry_at(repository, saved.path);
  CHECK(package.is_directory);
  CHECK(!package.children.empty());
  const auto latest = package.children.front();
  CHECK(!latest.container_item_id.empty());
  const auto keep = entry_at(repository, folder / "keep");
  const auto loose = entry_at(repository, folder / "loose.gin");

  // One version out of a package is not a file of its own.
  CHECK(!repository.can_move_patch(latest, keep));
  // A package lists its versions like a folder, but nothing goes into it.
  CHECK(!repository.can_move_patch(loose, package));
  CHECK(!repository.move_patch(loose, package));

  // The package file itself moves, and its versions' metadata with it.
  CHECK(repository.move_patch(package, keep));
  CHECK(fs::exists(folder / "keep" / saved.path.filename()));
  patches::FolderMetadataStore store(folder / ".megatoy" / "patches.json");
  CHECK(store.load());
  const auto followed =
      store.get("keep/" + saved.path.filename().generic_string() + "/latest");
  CHECK(followed.has_value());
  CHECK(followed->star_rating == 2);
}

void test_cross_storage_refused(const fs::path &root) {
  platform::StdFileSystem file_system;
  const auto first = root / "first";
  const auto second = root / "second";
  fs::create_directories(first);
  fs::create_directories(second / "sub");
  CHECK(patches::write_patch(make_patch(1), first / "a.gin"));
  CHECK(patches::write_patch(make_patch(2), second / "sub" / "b.gin"));

  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(first));
  CHECK(workspace.add(second));
  patches::PatchRepository repository(file_system, workspace);

  const auto file = entry_at(repository, first / "a.gin");
  CHECK(!repository.can_move_patch(file, entry_at(repository, second)));
  CHECK(!repository.can_move_patch(file, entry_at(repository, second / "sub")));
  CHECK(!repository.move_patch(file, entry_at(repository, second / "sub")));
  CHECK(fs::exists(first / "a.gin"));
}

void test_read_only_refused(const fs::path &root) {
  platform::StdFileSystem file_system;
  const auto builtin = root / "builtin";
  fs::create_directories(builtin / "sub");
  CHECK(patches::write_patch(make_patch(1), builtin / "preset.gin"));
  CHECK(patches::write_patch(make_patch(2), builtin / "sub" / "other.gin"));

  megatoy::workspace::Workspace workspace;
  patches::PatchRepository repository(file_system, workspace, builtin);
  const auto preset = entry_at(repository, builtin / "preset.gin");
  const auto sub = entry_at(repository, builtin / "sub");
  CHECK(!repository.can_move_patch(preset, sub));
  CHECK(!repository.move_patch(preset, sub));
  CHECK(fs::exists(builtin / "preset.gin"));
}

} // namespace

int main() {
  const auto temp =
      fs::temp_directory_path() / "megatoy_patch_repository_move_test";
  fs::remove_all(temp);
  fs::create_directories(temp);
  // The repository stores resolved paths; on macOS /var is /private/var.
  const auto root = fs::canonical(temp);

  test_file_move(root);
  test_directory_move(root);
  test_collision_refused(root);
  test_containers(root);
  test_cross_storage_refused(root);
  test_read_only_refused(root);

  fs::remove_all(root);
  std::cout << "All patch repository move tests passed\n";
  return 0;
}
