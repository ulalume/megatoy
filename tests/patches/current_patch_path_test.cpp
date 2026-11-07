#include "audio/audio_manager.hpp"
#include "core/status.hpp"
#include "formats/patch_registry.hpp"
#include "session_test_environment.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

using session_tests::LaunchedEnvironment;
using session_tests::find_patch_entry;

void test_current_patch_rename_preserves_clean_identity() {
  LaunchedEnvironment env("current_patch_path_test");
  auto patch = env.session.current_patch();
  patch.name = "embedded old name";
  const auto source_path = env.folder / "session-old.dmp";
  CHECK(patches::write_patch(patch, source_path));
  env.session.repository().refresh();

  const auto *found =
      find_patch_entry(env.session.repository().tree(), source_path);
  CHECK(found != nullptr);
  const auto entry = *found;
  ym2612::Patch loaded;
  CHECK(env.session.repository().load_patch(entry, loaded));
  env.session.set_current_patch(loaded, entry.relative_path);
  CHECK(env.session.current_patch().name == "session-old");
  CHECK(env.session.current_patch_is_user_patch());
  CHECK(!env.session.is_modified());

  env.session.current_patch().name = "format metadata changed";
  CHECK(env.session.is_modified());
  CHECK(env.session.current_patch_is_user_patch());
  env.session.mark_as_clean();

  CHECK(env.session.rename_patch(entry, "session-new"));
  CHECK(!std::filesystem::exists(source_path));
  CHECK(std::filesystem::exists(env.folder / "session-new.dmp"));
  CHECK(std::filesystem::path(env.session.current_patch_path()).stem() ==
        "session-new");
  CHECK(env.session.current_patch().name == "session-new");
  CHECK(env.session.capture_snapshot().original_patch.name == "session-new");
  CHECK(!env.session.is_modified());
}

void test_current_patch_path_recording() {
  LaunchedEnvironment env("current_patch_path_test");
  env.preferences.set_last_patch_path("patches/remembered.dmp");
  const auto url_patch = env.session.current_patch();
  env.session.set_current_patch(url_patch, {},
                                patches::PatchSession::RememberPatchPath::No);
  CHECK(env.preferences.last_patch_path() == "patches/remembered.dmp");

  const auto source_path = env.folder / "recorded.dmp";
  env.session.set_current_patch_path(source_path);
  CHECK(
      env.preferences.last_patch_path() ==
      env.session.repository().to_relative_path(source_path).generic_string());

  env.session.set_current_patch_path({});
  CHECK(env.preferences.last_patch_path().empty());
}

// Renaming a folder that holds the current patch has to carry the patch with
// it, and renaming an unrelated one has to leave it alone.
void test_folder_rename_follows_the_current_patch() {
  LaunchedEnvironment env("current_patch_path_test");
  const auto folder = env.folder / "pads";
  std::filesystem::create_directories(folder);
  std::filesystem::create_directories(env.folder / "elsewhere");

  auto patch = env.session.current_patch();
  const auto source_path = folder / "warm.dmp";
  CHECK(patches::write_patch(patch, source_path));
  // An empty directory does not show up in the tree, so the decoy needs
  // something in it too.
  CHECK(patches::write_patch(patch, env.folder / "elsewhere" / "x.dmp"));
  env.session.repository().refresh();

  const auto *found =
      find_patch_entry(env.session.repository().tree(), source_path);
  CHECK(found != nullptr);
  ym2612::Patch loaded;
  CHECK(env.session.repository().load_patch(*found, loaded));
  env.session.set_current_patch(loaded, found->relative_path);
  const auto path_before = env.session.current_patch_path();
  CHECK(!path_before.empty());

  // An unrelated folder: the current patch keeps the path it had.
  const auto *unrelated = find_patch_entry(env.session.repository().tree(),
                                           env.folder / "elsewhere");
  CHECK(unrelated != nullptr);
  CHECK(env.session.rename_patch(*unrelated, "somewhere"));
  CHECK(env.session.current_patch_path() == path_before);

  // The folder it lives in: the path follows.
  const auto *pads = find_patch_entry(env.session.repository().tree(), folder);
  CHECK(pads != nullptr);
  CHECK(pads->is_directory);
  const auto pads_entry = *pads;
  CHECK(env.session.rename_patch(pads_entry, "warm pads"));

  CHECK(!std::filesystem::exists(folder));
  CHECK(std::filesystem::exists(env.folder / "warm pads" / "warm.dmp"));
  CHECK(env.session.current_patch_path() != path_before);
  CHECK(std::filesystem::path(env.session.current_patch_path())
            .parent_path()
            .filename() == "warm pads");
  CHECK(env.session.current_patch().name == "warm");
}

// Moving the folder that holds the current patch, or the patch itself, has to
// carry the editor's path, the browser selection and the remembered path.
void test_move_follows_the_current_patch() {
  LaunchedEnvironment env("current_patch_path_test");
  auto &repository = env.session.repository();
  const auto crate = env.folder / "crate";
  const auto shelf = env.folder / "shelf";
  std::filesystem::create_directories(crate / "strings");
  std::filesystem::create_directories(shelf);
  auto patch = env.session.current_patch();
  CHECK(patches::write_patch(patch, crate / "strings" / "violin.dmp"));
  CHECK(patches::write_patch(patch, shelf / "filler.dmp"));
  CHECK(formats::PatchRegistry::instance()
            .save_package(".ginpkg", crate / "strings", "cello", patch)
            .has_value());
  repository.refresh();

  const auto *violin =
      find_patch_entry(repository.tree(), crate / "strings" / "violin.dmp");
  CHECK(violin != nullptr);
  CHECK(env.session.load_patch_from_entry(*violin));

  const auto *strings = find_patch_entry(repository.tree(), crate / "strings");
  const auto *shelf_entry = find_patch_entry(repository.tree(), shelf);
  CHECK(strings != nullptr && shelf_entry != nullptr);
  CHECK(env.session.can_move_patch(*strings, *shelf_entry));
  const auto strings_entry = *strings;
  const auto shelf_copy = *shelf_entry;
  CHECK(env.session.move_patch(strings_entry, shelf_copy));

  const auto moved_violin = shelf / "strings" / "violin.dmp";
  CHECK(std::filesystem::exists(moved_violin));
  CHECK(!std::filesystem::exists(crate / "strings"));
  const auto expected =
      repository.to_relative_path(moved_violin).generic_string();
  CHECK(env.session.current_patch_path() == expected);
  CHECK(env.session.current_patch_selection_path() == expected);
  CHECK(env.preferences.last_patch_path() == expected);
  CHECK(env.session.current_patch().name == "violin");

  // The current patch itself, back up to the workspace folder.
  const auto *moved = find_patch_entry(repository.tree(), moved_violin);
  const auto *root = find_patch_entry(repository.tree(), env.folder);
  CHECK(moved != nullptr && root != nullptr);
  const auto file_entry = *moved;
  const auto root_entry = *root;
  CHECK(env.session.move_patch(file_entry, root_entry));
  const auto at_root =
      repository.to_relative_path(env.folder / "violin.dmp").generic_string();
  CHECK(env.session.current_patch_path() == at_root);
  CHECK(env.preferences.last_patch_path() == at_root);

  // One version out of a package: the selection keeps naming that version.
  const auto *package =
      find_patch_entry(repository.tree(), shelf / "strings" / "cello.ginpkg");
  CHECK(package != nullptr && !package->children.empty());
  const auto version = package->children.back();
  CHECK(env.session.load_patch_from_entry(version));
  const auto suffix =
      version.relative_path.substr(package->relative_path.size());
  CHECK(env.session.current_patch_selection_path() ==
        package->relative_path + suffix);

  const auto *crate_entry = find_patch_entry(repository.tree(), crate);
  const auto *strings_now =
      find_patch_entry(repository.tree(), shelf / "strings");
  CHECK(crate_entry != nullptr && strings_now != nullptr);
  const auto crate_copy = *crate_entry;
  const auto strings_copy = *strings_now;
  CHECK(env.session.move_patch(strings_copy, crate_copy));
  const auto package_now =
      repository.to_relative_path(crate / "strings" / "cello.ginpkg")
          .generic_string();
  CHECK(env.session.current_patch_path() == package_now);
  CHECK(env.session.current_patch_selection_path() == package_now + suffix);
  CHECK(env.preferences.last_patch_path() == package_now);

  // A workspace folder is not something to move.
  const auto *crate_again = find_patch_entry(repository.tree(), crate);
  CHECK(crate_again != nullptr);
  CHECK(!env.session.can_move_patch(root_entry, *crate_again));
}

// A workspace folder is registered by path, so renaming one has to move the
// registration with it or the folder comes back missing.
void test_workspace_root_rename_follows_the_preference() {
  LaunchedEnvironment env("current_patch_path_test");
  const auto *root =
      find_patch_entry(env.session.repository().tree(), env.folder);
  CHECK(root != nullptr);
  CHECK(root->is_directory);
  CHECK(env.preferences.workspace().contains(env.folder));

  const auto entry = *root;
  CHECK(env.session.rename_patch(entry, "sound library"));

  const auto renamed = env.root / "sound library";
  CHECK(!std::filesystem::exists(env.folder));
  CHECK(std::filesystem::exists(renamed));

  CHECK(!env.preferences.workspace().contains(env.folder));
  CHECK(env.preferences.workspace().contains(renamed));
  CHECK(env.preferences.workspace().folders().size() == 1);

  // And the repository is looking at the new location.
  CHECK(find_patch_entry(env.session.repository().tree(), renamed) != nullptr);
}

void test_restore_last_patch() {
  LaunchedEnvironment env("current_patch_path_test");
  auto patch = env.session.current_patch();
  patch.name = "embedded name";
  patch.instrument.algorithm = 6;
  const auto source_path = env.folder / "restored.dmp";
  CHECK(patches::write_patch(patch, source_path));
  env.session.repository().refresh();

  const auto relative =
      env.session.repository().to_relative_path(source_path).generic_string();
  env.preferences.set_last_patch_path(relative);
  CHECK(env.session.restore_patch(env.preferences.last_patch_path()));
  CHECK(env.session.current_patch_path() == relative);
  CHECK(env.session.current_patch_selection_path() == relative);
  CHECK(env.session.current_patch().name == "restored");
  CHECK(env.session.current_patch().instrument.algorithm == 6);
  CHECK(!env.session.is_modified());

  const auto package_path = env.folder / "restored-package.ginpkg";
  CHECK(patches::write_patch(patch, package_path));
  env.session.repository().refresh();
  const auto package_relative =
      env.session.repository().to_relative_path(package_path).generic_string();
  env.preferences.set_last_patch_path(package_relative);
  CHECK(env.session.restore_patch(env.preferences.last_patch_path()));
  CHECK(env.session.current_patch_path() == package_relative);
  CHECK(env.session.current_patch_selection_path() ==
        package_relative + "/latest");
  CHECK(!env.session.is_modified());
}

void test_missing_last_patch_falls_back_silently() {
  LaunchedEnvironment env("current_patch_path_test");
  env.session.initialize_patch_defaults();
  const auto fallback = env.session.capture_snapshot();
  const std::string missing = "patches/missing.dmp";
  env.preferences.set_last_patch_path(missing);
  megatoy::status::clear();

  CHECK(!env.session.restore_patch(env.preferences.last_patch_path()));
  CHECK(env.session.capture_snapshot() == fallback);
  CHECK(env.preferences.last_patch_path() == missing);
  CHECK(megatoy::status::entries().empty());
}

void test_patch_snapshot_roundtrip() {
  LaunchedEnvironment env("current_patch_path_test");
  auto before = env.session.capture_snapshot();
  env.session.current_patch().name = "modified";
  env.session.restore_snapshot(before);
  CHECK(env.session.current_patch().name == before.patch.name);
}

void test_session_path_survives_workspace_relabel() {
  const auto root =
      std::filesystem::temp_directory_path() / "megatoy_path_relabel_test";
  std::filesystem::remove_all(root);
  platform::StdFileSystem fs;
  const auto config = root / "path-relabel-config";
  const auto first_input = root / "path-relabel-first" / "patches";
  const auto second_input = root / "path-relabel-second" / "patches";
  std::filesystem::create_directories(first_input);
  std::filesystem::create_directories(second_input);
  const auto first = std::filesystem::weakly_canonical(first_input);
  const auto second = std::filesystem::weakly_canonical(second_input);

  megatoy::system::PathService paths(fs, config);
  PreferenceManager preferences(paths);
  CHECK(preferences.add_workspace_folder(first));
  CHECK(preferences.add_workspace_folder(second));
  AudioManager audio;
  patches::PatchSession session(paths, preferences, audio);
  session.sync_workspace();

  ym2612::Patch patch;
  patch.name = "same name";
  const auto saved = session.repository().save_patch_in(
      second, patch, patch.name, /*overwrite=*/true, ".dmp");
  CHECK(saved.status == patches::SavePatchResult::Status::Success);
  session.set_current_patch_path(saved.path);
  const auto before = session.current_patch_path();
  CHECK(session.repository().to_absolute_path(before) == saved.path);

  CHECK(preferences.remove_workspace_folder(first));
  session.sync_workspace();
  CHECK(session.current_patch_path() != before);
  CHECK(!session.current_patch_path().empty());
  CHECK(session.repository().to_absolute_path(session.current_patch_path()) ==
        saved.path);
  std::filesystem::remove_all(root);
}

} // namespace

int main() {
  test_current_patch_rename_preserves_clean_identity();
  test_current_patch_path_recording();
  test_folder_rename_follows_the_current_patch();
  test_move_follows_the_current_patch();
  test_workspace_root_rename_follows_the_preference();
  test_restore_last_patch();
  test_missing_last_patch_falls_back_silently();
  test_patch_snapshot_roundtrip();
  test_session_path_survives_workspace_relabel();
  std::cout << "All current patch path tests passed\n";
  return 0;
}
