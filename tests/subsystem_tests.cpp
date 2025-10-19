#include "audio/audio_manager.hpp"
#include "patches/patch_editing_session.hpp"
#include "preferences/preference_manager.hpp"
#include "preferences/preferences_data.hpp"
#include "system/directory_service.hpp"
#include "ym2612/note.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>

namespace {

struct TestEnvironment {
  std::filesystem::path root;
  megatoy::system::DirectoryService directories;
  PreferenceManager preferences;
  AudioManager audio;
  PatchEditingSession session;

  TestEnvironment()
      : root(std::filesystem::temp_directory_path() /
             "megatoy_subsystem_tests"),
        directories(), preferences(directories), audio(),
        session(directories, audio) {
    std::filesystem::remove_all(root);
    directories.set_data_root(root);
    directories.ensure_directories();
    session.initialize_patch_defaults();
  }

  ~TestEnvironment() { std::filesystem::remove_all(root); }
};

void test_patch_snapshot_roundtrip(TestEnvironment &env) {
  auto before = env.session.capture_snapshot();
  env.session.current_patch().name = "modified";
  env.session.restore_snapshot(before);
  assert(env.session.current_patch().name == before.patch.name);
}

void test_note_allocation(TestEnvironment &env) {
  PreferenceManager::UIPreferences prefs{};
  prefs.use_velocity = true;
  prefs.steal_oldest_note_when_full = true;

  ym2612::Note c4 = ym2612::Note::from_midi_note(60);
  ym2612::Note e4 = ym2612::Note::from_midi_note(64);

  assert(env.session.note_on(c4, 90, prefs));
  assert(env.session.note_is_active(c4));
  assert(env.session.note_on(e4, 70, prefs));
  assert(env.session.note_is_active(e4));

  assert(env.session.note_off(c4));
  assert(!env.session.note_is_active(c4));
  env.session.release_all_notes();
  for (bool active : env.session.active_channels()) {
    assert(!active);
  }
}

} // namespace

int main() {
  TestEnvironment env;
  test_patch_snapshot_roundtrip(env);
  test_note_allocation(env);

  std::cout << "All subsystem tests passed\n";
  return 0;
}
