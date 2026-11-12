#include "../test_check.hpp"
#include "patches/patch_metadata.hpp"
#include "patches/patch_repository.hpp"
#include "patches/patch_write.hpp"
#include "platform/std_file_system.hpp"
#include "workspace/workspace.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

namespace {

patches::PatchEntry entry_at(const patches::PatchRepository &repository,
                             const fs::path &full_path) {
  const auto *found = repository.find_by_full_path(full_path);
  CHECK(found != nullptr);
  return *found;
}

ym2612::Patch make_patch(int algorithm) {
  ym2612::Patch patch;
  patch.instrument.algorithm =
      static_cast<decltype(patch.instrument.algorithm)>(algorithm);
  return patch;
}

void test_file_copy(const fs::path &root) {
  platform::StdFileSystem file_system;
  const auto folder = root / "files";
  fs::create_directories(folder / "pads");
  CHECK(patches::write_patch(make_patch(3), folder / "lead.dmp"));

  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(folder));
  patches::PatchRepository repository(file_system, workspace);

  patches::PatchMetadata metadata;
  metadata.star_rating = 4;
  CHECK(repository.update_patch_metadata(
      repository.to_relative_path(folder / "lead.dmp").generic_string(),
      metadata));
  repository.refresh();

  const auto file = entry_at(repository, folder / "lead.dmp");
  const auto pads = entry_at(repository, folder / "pads");
  CHECK(repository.can_copy_patch(file, pads));
  const auto copied = repository.copy_patch(file, pads, ".dmp");
  CHECK(copied.has_value() && *copied == folder / "pads" / "lead.dmp");
  CHECK(fs::exists(folder / "lead.dmp"));
  const auto *copy = repository.find_by_full_path(*copied);
  CHECK(copy != nullptr && copy->metadata.has_value());
  CHECK(copy->metadata->star_rating == 4);

  // Into its own folder it takes a name of its own.
  const auto root_entry = entry_at(repository, folder);
  CHECK(repository.copy_patch(file, root_entry, ".dmp") ==
        folder / "lead copy.dmp");
  CHECK(repository.copy_patch(file, root_entry, ".dmp") ==
        folder / "lead copy 2.dmp");
}

void test_directory_copy(const fs::path &root) {
  platform::StdFileSystem file_system;
  const auto folder = root / "dirs";
  fs::create_directories(folder / "bass" / "inner");
  fs::create_directories(folder / "low");
  CHECK(patches::write_patch(make_patch(2),
                             folder / "bass" / "inner" / "sub.dmp"));

  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(folder));
  patches::PatchRepository repository(file_system, workspace);

  patches::PatchMetadata metadata;
  metadata.icon = "Bass";
  CHECK(repository.update_patch_metadata(
      repository.to_relative_path(folder / "bass" / "inner" / "sub.dmp")
          .generic_string(),
      metadata));
  repository.refresh();

  const auto bass = entry_at(repository, folder / "bass");
  // Not into itself, nor into anything below it.
  CHECK(!repository.can_copy_patch(bass, bass));
  CHECK(!repository.can_copy_patch(
      bass, entry_at(repository, folder / "bass" / "inner")));

  const auto copied =
      repository.copy_patch(bass, entry_at(repository, folder / "low"), "");
  CHECK(copied == folder / "low" / "bass");
  CHECK(fs::exists(folder / "bass" / "inner" / "sub.dmp"));
  const auto *copy = repository.find_by_full_path(folder / "low" / "bass" /
                                                  "inner" / "sub.dmp");
  CHECK(copy != nullptr && copy->metadata.has_value());
  CHECK(copy->metadata->icon == "Bass");
}

void test_across_folders(const fs::path &root) {
  platform::StdFileSystem file_system;
  const auto builtin = root / "builtin";
  const auto first = root / "first";
  const auto second = root / "second";
  fs::create_directories(builtin);
  fs::create_directories(first);
  fs::create_directories(second);
  CHECK(patches::write_patch(make_patch(1), builtin / "preset.dmp"));
  CHECK(patches::write_patch(make_patch(2), first / "a.dmp"));

  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(first));
  CHECK(workspace.add(second));
  // Writability is cached policy; OS permissions differ.
  const_cast<megatoy::workspace::Folder *>(workspace.find(second))->writable =
      false;
  patches::PatchRepository repository(file_system, workspace, builtin);

  const auto preset = entry_at(repository, builtin / "preset.dmp");
  const auto a = entry_at(repository, first / "a.dmp");
  const auto first_entry = entry_at(repository, first);
  CHECK(repository.copy_patch(preset, first_entry, ".dmp") ==
        first / "preset.dmp");
  CHECK(fs::exists(builtin / "preset.dmp"));

  // Nothing goes into a folder megatoy may not write in.
  CHECK(!repository.can_copy_patch(a, entry_at(repository, second)));
  CHECK(!repository.can_copy_patch(a, entry_at(repository, builtin)));
}

void test_package_version_copy(const fs::path &root) {
  platform::StdFileSystem file_system;
  const auto folder = root / "packages";
  fs::create_directories(folder / "keep");

  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(folder));
  patches::PatchRepository repository(file_system, workspace);

  const auto saved = repository.save_patch(make_patch(5), "pack",
                                           /*overwrite=*/true, ".ginpkg");
  CHECK(saved.status == patches::SavePatchResult::Status::Success);
  const auto package = entry_at(repository, saved.path);
  CHECK(!package.children.empty());
  const auto latest = package.children.front();
  const auto keep = entry_at(repository, folder / "keep");

  // Nothing goes into a package.
  CHECK(!repository.can_copy_patch(latest, package));

  const auto copied = repository.copy_patch(latest, keep, ".dmp");
  CHECK(copied.has_value());
  CHECK(copied->parent_path() == folder / "keep");
  CHECK(copied->extension() == ".dmp");
  ym2612::Patch loaded;
  CHECK(repository.load_patch(entry_at(repository, *copied), loaded));
  CHECK(loaded.instrument.algorithm == 5);
}

void test_undo(const fs::path &root) {
  platform::StdFileSystem file_system;
  const auto folder = root / "undo";
  fs::create_directories(folder / "kit" / "inner");
  fs::create_directories(folder / "low");
  CHECK(
      patches::write_patch(make_patch(4), folder / "kit" / "inner" / "x.dmp"));
  CHECK(patches::write_patch(make_patch(6), folder / "snare.dmp"));

  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(folder));
  patches::PatchRepository repository(file_system, workspace);

  // A copy is taken away again, a folder with everything in it.
  const auto copied =
      repository.copy_patch(entry_at(repository, folder / "kit"),
                            entry_at(repository, folder / "low"), "");
  CHECK(copied.has_value());
  CHECK(repository.remove_copies({*copied}));
  CHECK(!fs::exists(folder / "low" / "kit"));
  CHECK(fs::exists(folder / "kit" / "inner" / "x.dmp"));

  // A deleted patch comes back with its metadata.
  patches::PatchMetadata metadata;
  metadata.star_rating = 3;
  CHECK(repository.update_patch_metadata(
      repository.to_relative_path(folder / "snare.dmp").generic_string(),
      metadata));
  repository.refresh();
  const auto snare = entry_at(repository, folder / "snare.dmp");
  const auto kept = repository.keep_for_restore(snare);
  CHECK(kept.has_value());
  CHECK(repository.delete_patch(snare));
  CHECK(!fs::exists(folder / "snare.dmp"));
  CHECK(repository.restore(*kept));
  // Not over something that has taken its place since.
  CHECK(!repository.restore(*kept));
  const auto restored = entry_at(repository, folder / "snare.dmp");
  CHECK(restored.metadata.has_value() && restored.metadata->star_rating == 3);
  ym2612::Patch loaded;
  CHECK(repository.load_patch(restored, loaded));
  CHECK(loaded.instrument.algorithm == 6);

  // So does a folder, with everything in it, its empty folders and its
  // patches' metadata too.
  fs::create_directories(folder / "kit" / "empty");
  CHECK(repository.update_patch_metadata(
      repository.to_relative_path(folder / "kit" / "inner" / "x.dmp")
          .generic_string(),
      metadata));
  repository.refresh();
  const auto kit = entry_at(repository, folder / "kit");
  CHECK(repository.can_delete_patch(kit));
  const auto kept_kit = repository.keep_for_restore(kit);
  CHECK(kept_kit.has_value() && kept_kit->is_directory);
  CHECK(repository.delete_patch(kit));
  CHECK(!fs::exists(folder / "kit"));
  CHECK(repository.restore(*kept_kit));
  CHECK(fs::is_directory(folder / "kit" / "empty"));
  const auto x = entry_at(repository, folder / "kit" / "inner" / "x.dmp");
  CHECK(x.metadata.has_value() && x.metadata->star_rating == 3);

  // Too large to keep: no undo offered.
  CHECK(!repository.keep_for_restore(kit, 1).has_value());

  // The workspace folder itself is only taken out of the workspace.
  CHECK(!repository.can_delete_patch(entry_at(repository, folder)));
}

void test_files_from_outside(const fs::path &root) {
  platform::StdFileSystem file_system;
  const auto folder = root / "inbox";
  const auto outside = root / "outside";
  fs::create_directories(folder);
  fs::create_directories(outside / "kit");
  CHECK(patches::write_patch(make_patch(2), outside / "lead.dmp"));
  CHECK(patches::write_patch(make_patch(3), outside / "kit" / "snare.dmp"));
  {
    std::ofstream(outside / "notes.txt") << "not a patch";
  }

  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(folder));
  patches::PatchRepository repository(file_system, workspace);

  std::size_t skipped = 0;
  const auto copies = repository.copy_files_in(
      {outside / "lead.dmp", outside / "kit", outside / "notes.txt"},
      entry_at(repository, folder), skipped);
  CHECK(copies.size() == 2);
  CHECK(skipped == 1);
  CHECK(fs::exists(folder / "lead.dmp"));
  CHECK(fs::exists(folder / "kit" / "snare.dmp"));
  CHECK(!fs::exists(folder / "notes.txt"));
  CHECK(fs::exists(outside / "lead.dmp"));

  // A folder brings only what a format reads, and with nothing so, it is not
  // copied at all.
  fs::create_directories(outside / "docs");
  {
    std::ofstream(outside / "kit" / "readme.txt") << "not a patch";
    std::ofstream(outside / "docs" / "specs.txt") << "not a patch";
  }
  const auto again =
      repository.copy_files_in({outside / "kit", outside / "docs"},
                               entry_at(repository, folder), skipped);
  CHECK(again.size() == 1 && skipped == 1);
  CHECK(fs::exists(folder / "kit copy" / "snare.dmp"));
  CHECK(!fs::exists(folder / "kit copy" / "readme.txt"));
  CHECK(!fs::exists(folder / "docs"));
  CHECK(repository.remove_copies(again));

  CHECK(repository.remove_copies(copies));
  CHECK(!fs::exists(folder / "lead.dmp") && !fs::exists(folder / "kit"));

  // Nor a folder into one inside it.
  fs::create_directories(outside / "kit" / "deeper");
  CHECK(workspace.add(outside));
  repository.sync_workspace();
  CHECK(repository
            .copy_files_in({outside / "kit"},
                           entry_at(repository, outside / "kit" / "deeper"),
                           skipped)
            .empty());
  CHECK(skipped == 1);
  CHECK(!fs::exists(outside / "kit" / "deeper" / "kit"));
}

} // namespace

int main() {
  const auto temp =
      fs::temp_directory_path() / "megatoy_patch_repository_copy_test";
  fs::remove_all(temp);
  fs::create_directories(temp);
  // The repository stores resolved paths; on macOS /var is /private/var.
  const auto root = fs::canonical(temp);

  test_file_copy(root);
  test_directory_copy(root);
  test_across_folders(root);
  test_package_version_copy(root);
  test_undo(root);
  test_files_from_outside(root);

  fs::remove_all(root);
  std::cout << "All patch repository copy tests passed\n";
  return 0;
}
