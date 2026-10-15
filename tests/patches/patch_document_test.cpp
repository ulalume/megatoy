#include "../test_check.hpp"
#include "audio/audio_manager.hpp"
#include "audio/audio_transport.hpp"
#include "patches/patch_session.hpp"
#include "patches/patch_write.hpp"
#include "platform/native/native_file_system.hpp"
#include "preferences/preference_manager.hpp"
#include "system/path_service.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

/// Opens and accepts commands; nothing is ever rendered.
class SilentTransport final : public AudioTransport {
public:
  bool start(std::uint32_t, RenderCallback callback) override {
    callback_ = std::move(callback);
    active_ = true;
    return true;
  }
  void stop() override {
    active_ = false;
    callback_ = {};
  }
  bool is_active() const override { return active_; }

private:
  RenderCallback callback_;
  bool active_ = false;
};

struct TestEnvironment {
  fs::path root;
  fs::path patches_folder;
  NativeFileSystem file_system;
  megatoy::system::PathService directories;
  PreferenceManager preferences;
  AudioManager audio;
  patches::PatchSession session;

  static fs::path make_root() {
    auto path = fs::temp_directory_path() / "megatoy_patch_document_tests";
    fs::remove_all(path);
    fs::create_directories(path / "config");
    fs::create_directories(path / "patches");
    return fs::weakly_canonical(path);
  }

  TestEnvironment()
      : root(make_root()), patches_folder(root / "patches"), file_system(),
        directories(file_system, root / "config"), preferences(directories),
        audio(std::make_unique<SilentTransport>()),
        session(directories, preferences, audio) {
    preferences.add_workspace_folder(patches_folder);
    session.sync_workspace();
    session.initialize_patch_defaults();
  }

  ~TestEnvironment() { fs::remove_all(root); }

  patches::PatchEntry entry_at(const fs::path &path) const {
    const auto *found = find(session.repository().tree(), path);
    CHECK(found != nullptr);
    return *found;
  }

  std::string relative(const fs::path &path) const {
    return session.repository().to_relative_path(path).generic_string();
  }

private:
  static const patches::PatchEntry *
  find(const std::vector<patches::PatchEntry> &entries, const fs::path &path) {
    for (const auto &entry : entries) {
      if (entry.full_path == path) {
        return &entry;
      }
      if (const auto *found = find(entry.children, path)) {
        return found;
      }
    }
    return nullptr;
  }
};

ym2612::Patch make_patch(int algorithm) {
  ym2612::Patch patch;
  patch.instrument.algorithm =
      static_cast<decltype(patch.instrument.algorithm)>(algorithm);
  return patch;
}

void test_open_activate_close() {
  TestEnvironment env;
  auto &session = env.session;
  CHECK(session.documents().size() == 1);
  const auto first = session.active_document_id();
  // The document a session starts with is a preview.
  CHECK(!session.active_document().pinned());

  const auto second = session.open_document(make_patch(2), {});
  const auto third = session.open_document(make_patch(3), {});
  CHECK(second != first && third != first && third != second);
  CHECK(session.documents().size() == 3);
  // Opening does not activate.
  CHECK(session.active_document_id() == first);

  CHECK(session.activate(second));
  CHECK(session.active_document_id() == second);
  CHECK(session.current_patch().instrument.algorithm == 2);
  CHECK(!session.activate(9999));
  CHECK(session.active_document_id() == second);

  // Closing an inactive document leaves the active one alone.
  CHECK(session.close_document(first));
  CHECK(session.active_document_id() == second);
  CHECK(session.find_document(first) == nullptr);
  CHECK(!session.close_document(first));

  // Closing the active one activates the next, or failing that the previous.
  const auto fourth = session.open_document(make_patch(4), {});
  CHECK(session.activate(third));
  CHECK(session.close_document(third));
  CHECK(session.active_document_id() == fourth);
  CHECK(session.close_document(fourth));
  CHECK(session.active_document_id() == second);
  CHECK(session.current_patch().instrument.algorithm == 2);
  CHECK(!session.close_document(9999));

  // Ids are never handed out twice.
  const auto fifth = session.open_default_document();
  CHECK(fifth != first && fifth != second && fifth != third && fifth != fourth);
  CHECK(session.find_document(fifth)->id() == fifth);
  CHECK(session.find_document(fifth)->pinned());
  const auto preview = session.open_document(make_patch(5), {}, {}, false);
  CHECK(!session.find_document(preview)->pinned());
}

void test_activation_reaches_the_chip() {
  TestEnvironment env;
  auto &session = env.session;
  CHECK(env.audio.initialize(44100));
  session.current_patch() = make_patch(1);
  CHECK(session.apply_patch_to_audio_if_changed());
  CHECK(!session.apply_patch_to_audio_if_changed());

  const auto other = session.open_document(make_patch(6), {});
  CHECK(!session.apply_patch_to_audio_if_changed());
  CHECK(session.activate(other));
  CHECK(session.apply_patch_to_audio_if_changed());
  CHECK(!session.apply_patch_to_audio_if_changed());
  env.audio.shutdown();
}

void test_find_by_selection_path() {
  TestEnvironment env;
  auto &session = env.session;
  const auto warm = env.patches_folder / "warm.gin";
  const auto cold = env.patches_folder / "cold.gin";
  CHECK(patches::write_patch(make_patch(1), warm));
  CHECK(patches::write_patch(make_patch(2), cold));
  session.repository().refresh();

  const auto warm_id = session.open_entry(env.entry_at(warm));
  CHECK(warm_id.has_value());
  CHECK(session.find_document(*warm_id)->path() == env.relative(warm));
  CHECK(session.find_document(*warm_id)->patch().name == "warm");
  CHECK(!session.find_document(*warm_id)->is_modified());

  CHECK(session.find_document_by_selection_path(env.relative(warm)) == warm_id);
  CHECK(session.find_document_by_selection_path(warm) == warm_id);
  CHECK(!session.find_document_by_selection_path(env.relative(cold)));
  CHECK(!session.find_document_by_selection_path({}));

  // A fresh document without a file matches nothing either.
  session.open_document(make_patch(3), {});
  CHECK(!session.find_document_by_selection_path({}));
}

void test_modified_per_document() {
  TestEnvironment env;
  auto &session = env.session;
  const auto first = session.active_document_id();
  const auto second = session.open_document(make_patch(2), {});
  CHECK(!session.any_document_modified());

  session.find_document(second)->patch().instrument.feedback = 3;
  CHECK(session.find_document(second)->is_modified());
  CHECK(!session.find_document(first)->is_modified());
  CHECK(!session.is_modified());
  CHECK(session.any_document_modified());
  CHECK(session.modified_document_count() == 1);

  session.current_patch().instrument.feedback = 5;
  CHECK(session.is_modified());
  CHECK(session.modified_document_count() == 2);

  session.mark_as_clean();
  CHECK(session.modified_document_count() == 1);
  session.mark_all_clean();
  CHECK(!session.any_document_modified());
}

void test_rename_and_move_follow_inactive_documents() {
  TestEnvironment env;
  auto &session = env.session;
  const auto pads = env.patches_folder / "pads";
  const auto shelf = env.patches_folder / "shelf";
  fs::create_directories(pads);
  fs::create_directories(shelf);
  CHECK(patches::write_patch(make_patch(1), pads / "warm.gin"));
  CHECK(patches::write_patch(make_patch(2), shelf / "filler.gin"));
  CHECK(patches::write_patch(make_patch(3), env.patches_folder / "lead.gin"));
  session.repository().refresh();

  // The active document is somewhere else entirely.
  CHECK(session.load_patch_from_entry(
      env.entry_at(env.patches_folder / "lead.gin")));
  const auto active_path = session.current_patch_path();
  CHECK(env.preferences.last_patch_path() == active_path);

  const auto warm = session.open_entry(env.entry_at(pads / "warm.gin"));
  CHECK(warm.has_value());

  CHECK(session.rename_patch(env.entry_at(pads), "warm pads"));
  const auto renamed = env.patches_folder / "warm pads" / "warm.gin";
  CHECK(session.find_document(*warm)->path() == env.relative(renamed));
  CHECK(session.find_document(*warm)->selection_path() ==
        env.relative(renamed));
  CHECK(session.current_patch_path() == active_path);
  // The remembered path belongs to the active document.
  CHECK(env.preferences.last_patch_path() == active_path);

  CHECK(session.move_patch(env.entry_at(env.patches_folder / "warm pads"),
                           env.entry_at(shelf)));
  const auto moved = shelf / "warm pads" / "warm.gin";
  CHECK(fs::exists(moved));
  CHECK(session.find_document(*warm)->path() == env.relative(moved));
  CHECK(session.current_patch_path() == active_path);
  CHECK(env.preferences.last_patch_path() == active_path);

  // Renaming the file itself renames the patch in that document only.
  CHECK(session.rename_patch(env.entry_at(moved), "warmer"));
  const auto warmer = shelf / "warm pads" / "warmer.gin";
  CHECK(session.find_document(*warm)->path() == env.relative(warmer));
  CHECK(session.find_document(*warm)->patch().name == "warmer");
  CHECK(!session.find_document(*warm)->is_modified());
  CHECK(session.current_patch().name == "lead");
}

void test_delete_clears_every_document_showing_it() {
  TestEnvironment env;
  auto &session = env.session;
  const auto doomed = env.patches_folder / "doomed.gin";
  const auto kept = env.patches_folder / "kept.gin";
  CHECK(patches::write_patch(make_patch(1), doomed));
  CHECK(patches::write_patch(make_patch(2), kept));
  session.repository().refresh();

  CHECK(session.load_patch_from_entry(env.entry_at(kept)));
  const auto inactive = session.open_entry(env.entry_at(doomed));
  CHECK(inactive.has_value());

  CHECK(session.delete_patch(env.entry_at(doomed)));
  CHECK(!fs::exists(doomed));
  CHECK(session.find_document(*inactive)->path().empty());
  CHECK(session.find_document(*inactive)->selection_path().empty());
  CHECK(session.current_patch_path() == env.relative(kept));
  CHECK(env.preferences.last_patch_path() == env.relative(kept));

  // The active document's own file: it and the preference both let go.
  CHECK(session.delete_patch(env.entry_at(kept)));
  CHECK(session.current_patch_path().empty());
  CHECK(env.preferences.last_patch_path().empty());
}

void test_workspace_relabel_follows_inactive_documents() {
  TestEnvironment env;
  auto &session = env.session;
  const auto other_input = env.root / "other" / "patches";
  fs::create_directories(other_input);
  const auto other = fs::weakly_canonical(other_input);
  CHECK(env.preferences.add_workspace_folder(other));
  session.sync_workspace();

  const auto saved = other / "same.gin";
  CHECK(patches::write_patch(make_patch(4), saved));
  session.repository().refresh();
  const auto inactive = session.open_entry(env.entry_at(saved));
  CHECK(inactive.has_value());
  const auto before = session.find_document(*inactive)->path();

  CHECK(env.preferences.remove_workspace_folder(env.patches_folder));
  session.sync_workspace();
  const auto after = session.find_document(*inactive)->path();
  CHECK(after != before);
  CHECK(!after.empty());
  CHECK(session.repository().to_absolute_path(after) == saved);
}

void test_snapshot_per_document() {
  TestEnvironment env;
  auto &session = env.session;
  const auto first = session.active_document_id();
  const auto first_before = session.capture_snapshot();
  const auto second = session.open_document(make_patch(2), {});
  const auto snapshot = session.find_document(second)->capture_snapshot();

  session.find_document(second)->patch().instrument.algorithm = 7;
  session.restore_snapshot(second, snapshot);
  CHECK(session.find_document(second)->patch().instrument.algorithm == 2);
  CHECK(!session.find_document(second)->is_modified());
  CHECK(session.active_document_id() == first);
  CHECK(session.capture_snapshot() == first_before);

  // Restoring into a closed document does nothing.
  CHECK(session.close_document(second));
  session.restore_snapshot(second, snapshot);
  CHECK(session.documents().size() == 1);
  CHECK(session.capture_snapshot() == first_before);
}

struct BrowserFixture {
  TestEnvironment env;
  fs::path warm;
  fs::path cold;
  fs::path lead;

  BrowserFixture() {
    warm = env.patches_folder / "warm.gin";
    cold = env.patches_folder / "cold.gin";
    lead = env.patches_folder / "lead.gin";
    CHECK(patches::write_patch(make_patch(1), warm));
    CHECK(patches::write_patch(make_patch(2), cold));
    CHECK(patches::write_patch(make_patch(3), lead));
    env.session.repository().refresh();
  }
};

void test_preview_is_reused() {
  BrowserFixture fixture;
  auto &env = fixture.env;
  auto &session = env.session;
  const auto preview = session.active_document_id();

  const auto warm = session.open_or_activate(env.entry_at(fixture.warm), false);
  CHECK(warm == preview);
  CHECK(session.documents().size() == 1);
  CHECK(session.active_document_id() == preview);
  CHECK(session.current_patch_path() == env.relative(fixture.warm));
  CHECK(session.current_patch().instrument.algorithm == 1);
  CHECK(!session.active_document().pinned());
  CHECK(env.preferences.last_patch_path() == env.relative(fixture.warm));

  const auto cold = session.open_or_activate(env.entry_at(fixture.cold), false);
  CHECK(cold == preview);
  CHECK(session.documents().size() == 1);
  CHECK(session.current_patch_path() == env.relative(fixture.cold));
  CHECK(session.active_document().selection_path() ==
        env.relative(fixture.cold));
  CHECK(session.current_patch().instrument.algorithm == 2);
  CHECK(!session.is_modified());
  CHECK(session.preview_document_id() == preview);
}

void test_open_beside_preview() {
  BrowserFixture fixture;
  auto &env = fixture.env;
  auto &session = env.session;
  const auto warm = session.open_or_activate(env.entry_at(fixture.warm), false);
  CHECK(warm.has_value());

  const auto cold = session.open_or_activate(env.entry_at(fixture.cold), true,
                                             /*reuse_preview=*/false);
  CHECK(cold.has_value() && *cold != *warm);
  CHECK(session.documents().size() == 2);
  CHECK(session.active_document_id() == *cold);
  CHECK(session.find_document(*cold)->pinned());
  CHECK(session.preview_document_id() == warm);
  CHECK(session.find_document(*warm)->path() == env.relative(fixture.warm));
}

void test_open_entry_activates_open_document() {
  BrowserFixture fixture;
  auto &env = fixture.env;
  auto &session = env.session;
  const auto warm = session.open_or_activate(env.entry_at(fixture.warm), true);
  CHECK(warm.has_value());
  CHECK(session.find_document(*warm)->pinned());
  const auto cold = session.open_or_activate(env.entry_at(fixture.cold), false);
  CHECK(cold.has_value() && *cold != *warm);
  CHECK(session.documents().size() == 2);
  CHECK(session.active_document_id() == *cold);

  CHECK(session.open_or_activate(env.entry_at(fixture.warm), false) == warm);
  CHECK(session.active_document_id() == *warm);
  CHECK(session.documents().size() == 2);
  // The preview stays where it is.
  CHECK(session.preview_document_id() == cold);
  CHECK(session.find_document(*cold)->path() == env.relative(fixture.cold));
}

void test_modified_preview_is_kept() {
  BrowserFixture fixture;
  auto &env = fixture.env;
  auto &session = env.session;
  const auto warm = session.open_or_activate(env.entry_at(fixture.warm), false);
  CHECK(warm.has_value());
  session.current_patch().instrument.feedback = 6;

  const auto cold = session.open_or_activate(env.entry_at(fixture.cold), false);
  CHECK(cold.has_value() && *cold != *warm);
  CHECK(session.documents().size() == 2);
  CHECK(session.find_document(*warm)->pinned());
  CHECK(session.find_document(*warm)->is_modified());
  CHECK(session.find_document(*warm)->path() == env.relative(fixture.warm));
  CHECK(!session.find_document(*cold)->pinned());
  CHECK(session.active_document_id() == *cold);
  CHECK(session.preview_document_id() == cold);
}

void test_pinning() {
  BrowserFixture fixture;
  auto &env = fixture.env;
  auto &session = env.session;

  // A double click opens and keeps it at once.
  const auto warm = session.open_or_activate(env.entry_at(fixture.warm), true);
  CHECK(warm.has_value());
  CHECK(session.find_document(*warm)->pinned());
  CHECK(!session.preview_document_id());

  // A single click and then a double click on the same entry.
  const auto cold = session.open_or_activate(env.entry_at(fixture.cold), false);
  CHECK(cold.has_value() && *cold != *warm);
  CHECK(!session.find_document(*cold)->pinned());
  CHECK(session.open_or_activate(env.entry_at(fixture.cold), true) == cold);
  CHECK(session.find_document(*cold)->pinned());
  CHECK(session.documents().size() == 2);

  const auto lead = session.open_or_activate(env.entry_at(fixture.lead), false);
  CHECK(lead.has_value() && *lead != *cold && *lead != *warm);
  CHECK(session.documents().size() == 3);
  CHECK(session.pin(*lead));
  CHECK(!session.pin(9999));
  CHECK(!session.preview_document_id());

  // Only one preview at a time: the next one opens beside the kept ones and
  // then keeps being reused.
  const auto again =
      session.open_or_activate(env.entry_at(fixture.warm), false);
  CHECK(again == warm);
  CHECK(!session.preview_document_id());
  session.close_document(*warm);
  const auto preview =
      session.open_or_activate(env.entry_at(fixture.warm), false);
  CHECK(preview.has_value());
  const auto reused = session.open_or_activate(
      env.entry_at(env.patches_folder / "cold.gin"), false);
  CHECK(reused == cold);
  std::size_t previews = 0;
  for (const auto &document : session.documents()) {
    previews += document->pinned() ? 0 : 1;
  }
  CHECK(previews == 1);

  // Editing a preview pins it.
  CHECK(session.activate(*preview));
  session.current_patch().instrument.algorithm = 7;
  session.pin_modified_documents();
  CHECK(session.find_document(*preview)->pinned());
  CHECK(!session.preview_document_id());
}

void test_closing_the_last_leaves_a_default_preview() {
  BrowserFixture fixture;
  auto &env = fixture.env;
  auto &session = env.session;
  const auto warm = session.open_or_activate(env.entry_at(fixture.warm), true);
  CHECK(warm.has_value());
  CHECK(session.documents().size() == 1);

  CHECK(session.close_document(*warm));
  CHECK(session.documents().size() == 1);
  CHECK(session.find_document(*warm) == nullptr);
  CHECK(session.active_document_id() != *warm);
  CHECK(!session.active_document().pinned());
  CHECK(!session.is_modified());
  CHECK(session.current_patch() == session.default_patch());
}

void test_dropped_patch_follows_the_preview_rules() {
  BrowserFixture fixture;
  auto &env = fixture.env;
  auto &session = env.session;
  const auto outside = env.root / "dropped.gin";
  CHECK(patches::write_patch(make_patch(5), outside));

  const auto first =
      session.open_or_activate_patch(make_patch(5), outside, false);
  CHECK(session.documents().size() == 1);
  CHECK(!session.find_document(first)->pinned());
  CHECK(session.current_patch().instrument.algorithm == 5);

  // The same file again is the same document.
  CHECK(session.open_or_activate_patch(make_patch(5), outside, false) == first);
  // A different instrument from that file is not.
  CHECK(session.pin(first));
  const auto other =
      session.open_or_activate_patch(make_patch(6), outside, false);
  CHECK(other != first);
  CHECK(session.documents().size() == 2);
}

void test_new_patch_opens_pinned() {
  BrowserFixture fixture;
  auto &env = fixture.env;
  auto &session = env.session;
  const auto before = session.documents().size();
  const auto result =
      session.create_patch_in(env.patches_folder, "fresh", ".gin");
  CHECK(result.is_success());
  CHECK(session.current_patch_path() ==
        env.relative(env.patches_folder / "fresh.gin"));
  CHECK(session.active_document().pinned());
  // The unedited preview the session started with was taken over.
  CHECK(session.documents().size() == before);
}

void test_open_documents_round_trip() {
  BrowserFixture fixture;
  auto &env = fixture.env;
  auto &session = env.session;
  const auto warm = session.open_or_activate(env.entry_at(fixture.warm), true);
  const auto cold = session.open_or_activate(env.entry_at(fixture.cold), true);
  const auto lead = session.open_or_activate(env.entry_at(fixture.lead), false);
  CHECK(warm && cold && lead);
  // Unsaved changes and documents without a file are not kept.
  session.find_document(*cold)->patch().instrument.feedback = 1;
  session.open_document(make_patch(4), {});
  CHECK(session.activate(*cold));
  session.remember_open_documents();

  CHECK(env.preferences.open_patches().size() == 3);
  CHECK(env.preferences.active_open_patch() == 1);

  // A fresh preference manager reads them back from disk.
  PreferenceManager preferences(env.directories);
  CHECK(preferences.open_patches() == env.preferences.open_patches());
  CHECK(preferences.active_open_patch() == 1);
  AudioManager audio(std::make_unique<SilentTransport>());
  patches::PatchSession restored(env.directories, preferences, audio);
  restored.sync_workspace();
  restored.initialize_patch_defaults();
  CHECK(restored.restore_open_documents());

  const auto &documents = restored.documents();
  CHECK(documents.size() == 3);
  CHECK(documents[0]->path() == env.relative(fixture.warm));
  CHECK(documents[1]->path() == env.relative(fixture.cold));
  CHECK(documents[2]->path() == env.relative(fixture.lead));
  CHECK(documents[0]->pinned());
  CHECK(documents[1]->pinned());
  CHECK(!documents[2]->pinned());
  CHECK(restored.active_document_id() == documents[1]->id());
  CHECK(!restored.any_document_modified());
  CHECK(restored.current_patch().instrument.feedback ==
        make_patch(2).instrument.feedback);
  CHECK(preferences.last_patch_path() == env.relative(fixture.cold));

  // A file that is gone is skipped.
  fs::remove(fixture.warm);
  restored.repository().refresh();
  AudioManager audio_again(std::make_unique<SilentTransport>());
  patches::PatchSession again(env.directories, preferences, audio_again);
  again.sync_workspace();
  CHECK(again.restore_open_documents());
  CHECK(again.documents().size() == 2);
  CHECK(again.current_patch_path() == env.relative(fixture.cold));
}

void test_nothing_to_restore() {
  TestEnvironment env;
  CHECK(!env.session.restore_open_documents());
  CHECK(env.session.documents().size() == 1);
}

} // namespace

void test_find_document_with_sound() {
  TestEnvironment env;
  auto &session = env.session;
  const auto second = session.open_document(make_patch(5), {});
  const auto third = session.open_document(make_patch(6), {});

  CHECK(session.find_document_with_sound(make_patch(6)) == third);
  CHECK(session.find_document_with_sound(make_patch(5)) == second);
  CHECK(!session.find_document_with_sound(make_patch(7)));

  // An edit changes what the document plays.
  session.find_document(third)->patch().instrument.feedback = 4;
  CHECK(!session.find_document_with_sound(make_patch(6)));

  // The active document wins when two play alike.
  const auto twin = session.open_document(make_patch(5), {});
  session.activate(twin);
  CHECK(session.find_document_with_sound(make_patch(5)) == twin);
}

int main() {
  test_open_activate_close();
  test_activation_reaches_the_chip();
  test_find_by_selection_path();
  test_modified_per_document();
  test_rename_and_move_follow_inactive_documents();
  test_delete_clears_every_document_showing_it();
  test_workspace_relabel_follows_inactive_documents();
  test_snapshot_per_document();
  test_preview_is_reused();
  test_open_beside_preview();
  test_open_entry_activates_open_document();
  test_modified_preview_is_kept();
  test_pinning();
  test_closing_the_last_leaves_a_default_preview();
  test_dropped_patch_follows_the_preview_rules();
  test_new_patch_opens_pinned();
  test_open_documents_round_trip();
  test_nothing_to_restore();
  test_find_document_with_sound();
  return 0;
}
