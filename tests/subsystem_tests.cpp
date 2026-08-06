#include "audio/audio_manager.hpp"
#include "patches/patch_write.hpp"
#include "formats/patch_registry.hpp"
#include "patches/patch_session.hpp"
#include "platform/native/native_file_system.hpp"
#include "preferences/preference_manager.hpp"
#include "system/path_service.hpp"
#include "ym2612/note.hpp"

#include "test_check.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

// Everything lives under a temporary root, including the configuration, so a
// test run can never touch the developer's real preferences or patches.
struct TestEnvironment {
  std::filesystem::path root;
  std::filesystem::path patches_folder;
  NativeFileSystem fs;
  megatoy::system::PathService directories;
  PreferenceManager preferences;
  AudioManager audio;
  patches::PatchSession session;

  static std::filesystem::path make_root() {
    auto path =
        std::filesystem::temp_directory_path() / "megatoy_subsystem_tests";
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path / "config");
    std::filesystem::create_directories(path / "patches");
    // The workspace resolves symlinks, and on macOS the temp directory is
    // one; resolve here too so path comparisons below line up.
    return std::filesystem::weakly_canonical(path);
  }

  TestEnvironment()
      : root(make_root()), patches_folder(root / "patches"), fs(),
        directories(fs, root / "config"), preferences(directories), audio(),
        session(directories, preferences, audio) {
    preferences.add_workspace_folder(patches_folder);
    session.sync_workspace();
    session.initialize_patch_defaults();
  }

  ~TestEnvironment() { std::filesystem::remove_all(root); }
};

void test_workspace_folder_is_visible(TestEnvironment &env) {
  const auto &folders = env.preferences.workspace().folders();
  CHECK(folders.size() == 1);
  CHECK(folders[0].available);
  CHECK(folders[0].writable);
  CHECK(env.preferences.workspace().default_save_folder().has_value());

  // The folder must show up as a root in the browser even while it is empty.
  const auto &tree = env.session.repository().tree();
  CHECK(!tree.empty());
  const bool found =
      std::any_of(tree.begin(), tree.end(), [&](const patches::PatchEntry &e) {
        return e.is_directory && e.full_path == env.patches_folder;
      });
  CHECK(found);
}

// Saving must land in the workspace folder, and its metadata must end up in
// the folder's own sidecar rather than anywhere global.
void test_save_and_metadata_roundtrip(TestEnvironment &env) {
  env.session.current_patch().name = "workspace test";
  env.session.current_patch().instrument.algorithm = 5;

  auto &repository = env.session.repository();
  const auto saved = repository.save_patch(env.session.current_patch(),
                                           "workspace test",
                                           /*overwrite=*/true, ".ginpkg");
  CHECK(saved.status == patches::SavePatchResult::Status::Success);
  CHECK(saved.path.parent_path() == env.patches_folder);
  CHECK(std::filesystem::exists(saved.path));

  repository.refresh();
  const auto relative = repository.to_relative_path(saved.path).generic_string();

  patches::PatchMetadata metadata;
  metadata.star_rating = 4;
  metadata.category = "bass";
  CHECK(repository.save_patch_metadata(relative, env.session.current_patch(),
                                       metadata));

  // The sidecar lives inside the folder, not in the user's config directory.
  const auto sidecar = env.patches_folder / ".megatoy" / "patches.json";
  CHECK(std::filesystem::exists(sidecar));

  const auto stored = repository.get_patch_metadata(relative);
  CHECK(stored.has_value());
  CHECK(stored->star_rating == 4);
  CHECK(stored->category == "bass");

  // A fresh repository over the same folder must see the ratings again --
  // that is the point of storing them next to the patches.
  megatoy::workspace::Workspace reopened;
  CHECK(reopened.add(env.patches_folder));
  patches::PatchRepository second(env.fs, reopened);
  const auto reloaded = second.get_patch_metadata(relative);
  CHECK(reloaded.has_value());
  CHECK(reloaded->star_rating == 4);
}

// Save must not overwrite a file it cannot safely rewrite.
void test_save_in_place_rules(TestEnvironment &env) {
  auto &repository = env.session.repository();
  const auto saved = repository.save_patch(env.session.current_patch(),
                                           "in place", /*overwrite=*/true,
                                           ".gin");
  CHECK(saved.status == patches::SavePatchResult::Status::Success);
  repository.refresh();

  // A .gin in a writable workspace folder is a single patch we can rewrite.
  env.session.set_current_patch_path(saved.path);
  CHECK(env.session.can_save_in_place());

  // A patch with no file behind it has to go through Save As.
  env.session.set_current_patch_path({});
  CHECK(!env.session.can_save_in_place());

  // So does one instrument taken out of a bank: rewriting the file would
  // discard every other instrument in it.
  const auto bank = env.patches_folder / "bank.mml";
  CHECK(formats::PatchRegistry::instance().write_text(
      ".mml", env.session.current_patch(), bank));
  repository.refresh();
  env.session.set_current_patch_path(bank);
  CHECK(!env.session.can_save_in_place());
}

void test_patch_snapshot_roundtrip(TestEnvironment &env) {
  auto before = env.session.capture_snapshot();
  env.session.current_patch().name = "modified";
  env.session.restore_snapshot(before);
  CHECK(env.session.current_patch().name == before.patch.name);
}

void test_note_allocation(TestEnvironment &env) {
  PreferenceManager::UIPreferences prefs{};
  prefs.use_velocity = true;
  prefs.steal_oldest_note_when_full = true;

  ym2612::Note c4 = ym2612::Note::from_midi_note(60);
  ym2612::Note e4 = ym2612::Note::from_midi_note(64);

  CHECK(env.session.note_on(c4, 90, prefs));
  CHECK(env.session.note_is_active(c4));
  CHECK(env.session.note_on(e4, 70, prefs));
  CHECK(env.session.note_is_active(e4));

  CHECK(env.session.note_off(c4));
  CHECK(!env.session.note_is_active(c4));
  env.session.release_all_notes();
  for (bool active : env.session.active_channels()) {
    CHECK(!active);
  }
}

std::string file_bytes(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

// "Duplicate" must create a new file and leave the original alone. It used to
// route through the ordinary save, which overwrote the original in place --
// and for formats that store no patch name that was a byte-identical write,
// so it looked like nothing had happened at all.
void test_duplicate_creates_new_file(TestEnvironment &env) {
  env.session.current_patch().name = "dup source";
  env.session.current_patch().instrument.algorithm = 2;
  const auto source = env.patches_folder / "dup source.ginpkg";
  CHECK(patches::write_patch(env.session.current_patch(), source));
  env.session.set_current_patch(env.session.current_patch(), source);
  env.session.repository().refresh();

  const std::string original = file_bytes(source);

  auto result = env.session.duplicate_current_patch("dup copy");
  CHECK(result.is_success());
  CHECK(result.path == env.patches_folder / "dup copy.ginpkg");
  CHECK(std::filesystem::exists(result.path));

  // The original file is byte-for-byte untouched...
  CHECK(file_bytes(source) == original);

  // ...and the working patch is now the duplicate.
  CHECK(env.session.current_patch().name == "dup copy");
  CHECK(env.session.repository().to_absolute_path(
            env.session.current_patch_path()) == result.path);

  // Duplicating onto an existing name refuses instead of overwriting.
  env.session.set_current_patch(env.session.current_patch(), source);
  auto conflict = env.session.duplicate_current_patch("dup copy");
  CHECK(conflict.is_error());
  CHECK(std::filesystem::exists(result.path));
}

} // namespace

int main() {
  TestEnvironment env;
  test_patch_snapshot_roundtrip(env);
  test_note_allocation(env);
  test_workspace_folder_is_visible(env);
  test_duplicate_creates_new_file(env);
  test_save_and_metadata_roundtrip(env);
  test_save_in_place_rules(env);

  std::cout << "All subsystem tests passed\n";
  return 0;
}
