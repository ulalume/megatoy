#include "audio/audio_manager.hpp"
#include "formats/patch_registry.hpp"
#include "patches/folder_metadata.hpp"
#include "patches/patch_session.hpp"
#include "patches/patch_write.hpp"
#include "platform/native/native_file_system.hpp"
#include "preferences/preference_manager.hpp"
#include "system/path_service.hpp"
#include "ym2612/note.hpp"

#include "test_check.hpp"
#include <SQLiteCpp/Database.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace {

class ScopedTestHome {
public:
  explicit ScopedTestHome(const std::filesystem::path &home)
#if defined(_WIN32)
      : variable_("USERPROFILE")
#else
      : variable_("HOME")
#endif
  {
    if (const char *previous = std::getenv(variable_.c_str())) {
      previous_ = previous;
    }
#if defined(_WIN32)
    CHECK(_putenv_s(variable_.c_str(), home.string().c_str()) == 0);
#else
    CHECK(setenv(variable_.c_str(), home.string().c_str(), 1) == 0);
#endif
  }

  ~ScopedTestHome() {
#if defined(_WIN32)
    _putenv_s(variable_.c_str(), previous_.value_or("").c_str());
#else
    if (previous_) {
      setenv(variable_.c_str(), previous_->c_str(), 1);
    } else {
      unsetenv(variable_.c_str());
    }
#endif
  }

private:
  std::string variable_;
  std::optional<std::string> previous_;
};

void test_fresh_and_legacy_default_workspace_migration(
    const std::filesystem::path &root) {
  NativeFileSystem fs;

  // A genuinely fresh installation has neither old preferences nor the old
  // auto-created patch tree, so it starts with no selected folder.
  const auto fresh_config = root / "fresh-config";
  {
    megatoy::system::PathService paths(fs, fresh_config);
    PreferenceManager preferences(paths);
    CHECK(preferences.workspace().empty());
  }
  {
    nlohmann::json fresh_preferences;
    std::ifstream input(fresh_config / "preferences.json");
    input >> fresh_preferences;
    CHECK(fresh_preferences.at("legacy_workspace_migration").get<int>() == 1);
    CHECK(fresh_preferences.at("legacy_metadata_migration").get<int>() == 1);
    CHECK(fresh_preferences.at("workspace_folders").empty());
  }

  // Old versions created this tree even when the user never changed a setting
  // (and therefore never wrote preferences.json).
  const auto legacy_patches =
      root / "home" / "Documents" / "megatoy" / "patches";
  std::filesystem::create_directories(legacy_patches / "user");
  const auto legacy_config = root / "launch-marker-config";
  {
    megatoy::system::PathService paths(fs, legacy_config);
    PreferenceManager preferences(paths);
    const auto &folders = preferences.workspace().folders();
    CHECK(folders.size() == 1);
    CHECK(folders[0].path == std::filesystem::weakly_canonical(legacy_patches));
  }

  // The adopted folder survives an ordinary restart as current-schema state.
  {
    megatoy::system::PathService paths(fs, legacy_config);
    PreferenceManager preferences(paths);
    CHECK(preferences.workspace().folders().size() == 1);
    CHECK(preferences.remove_workspace_folder(legacy_patches));
    CHECK(preferences.workspace().empty());
  }

  // Once removed, workspace_folders: [] is ordinary current-schema state and
  // the still-existing old directory must never be special-cased again.
  {
    megatoy::system::PathService paths(fs, legacy_config);
    PreferenceManager preferences(paths);
    CHECK(preferences.workspace().empty());
  }

  nlohmann::json rewritten;
  {
    std::ifstream input(legacy_config / "preferences.json");
    input >> rewritten;
  }
  CHECK(rewritten.at("workspace_folders").empty());

  // The first workspace release could already have erased data_directory and
  // saved an empty list. Without our marker this still needs the one-time
  // compatibility adoption.
  const auto incompatible_config = root / "incompatible-config";
  std::filesystem::create_directories(incompatible_config);
  {
    std::ofstream output(incompatible_config / "preferences.json");
    output << nlohmann::json{{"workspace_folders", nlohmann::json::array()}}
                  .dump(2);
  }
  {
    megatoy::system::PathService paths(fs, incompatible_config);
    PreferenceManager preferences(paths);
    CHECK(preferences.workspace().folders().size() == 1);
    CHECK(preferences.workspace().folders()[0].path ==
          std::filesystem::weakly_canonical(legacy_patches));
  }
  {
    std::ifstream input(incompatible_config / "preferences.json");
    input >> rewritten;
  }
  CHECK(rewritten.at("legacy_workspace_migration").get<int>() == 1);
  CHECK(rewritten.at("workspace_folders").size() == 1);
}

void test_legacy_data_directory_migration() {
  const auto root = std::filesystem::temp_directory_path() /
                    "megatoy_legacy_preferences_test";
  std::filesystem::remove_all(root);
  const auto config = root / "config";
  const auto legacy_root = root / "legacy-data";
  const auto patches_root = legacy_root / "patches";
  std::filesystem::create_directories(config);
  std::filesystem::create_directories(patches_root / "user");

  {
    std::ofstream output(config / "preferences.json");
    output << nlohmann::json{{"data_directory", legacy_root.string()}}.dump(2);
  }

  NativeFileSystem fs;
  megatoy::system::PathService paths(fs, config);
  {
    PreferenceManager preferences(paths);
    const auto &folders = preferences.workspace().folders();
    CHECK(folders.size() == 1);
    CHECK(folders[0].path == std::filesystem::weakly_canonical(patches_root));
    CHECK(preferences.last_save_directory() ==
          std::filesystem::weakly_canonical(patches_root));
  }

  nlohmann::json rewritten;
  {
    std::ifstream input(config / "preferences.json");
    input >> rewritten;
  }
  CHECK(!rewritten.contains("data_directory"));
  CHECK(rewritten.at("legacy_workspace_migration").get<int>() == 1);
  CHECK(rewritten.at("workspace_folders").size() == 1);
  CHECK(rewritten.at("workspace_folders")[0].get<std::string>() ==
        std::filesystem::weakly_canonical(patches_root).string());

  // An explicit current-schema empty list always wins, even if a stale old
  // key somehow remains in the file, once our migration marker is present.
  rewritten["workspace_folders"] = nlohmann::json::array();
  rewritten["data_directory"] = legacy_root.string();
  {
    std::ofstream output(config / "preferences.json");
    output << rewritten.dump(2);
  }
  {
    PreferenceManager preferences(paths);
    CHECK(preferences.workspace().empty());
  }

  std::filesystem::remove_all(root);
}

void create_legacy_metadata_database(const std::filesystem::path &path) {
  SQLite::Database database(path.string(),
                            SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
  database.exec(R"(
    CREATE TABLE patch_metadata (
      path TEXT PRIMARY KEY,
      hash TEXT NOT NULL,
      star_rating INTEGER DEFAULT 0,
      category TEXT DEFAULT '',
      notes TEXT DEFAULT '',
      created_at TEXT NOT NULL,
      updated_at TEXT NOT NULL
    )
  )");
  database.exec(R"(
    CREATE TABLE patch_tags (
      path TEXT NOT NULL,
      tag TEXT NOT NULL,
      PRIMARY KEY (path, tag)
    )
  )");
  database.exec(R"(
    INSERT INTO patch_metadata
      (path, hash, star_rating, category, notes, created_at, updated_at)
    VALUES
      ('user/bass.gin', 'legacy-bass-hash', 5, 'legacy bass', 'keep me',
       '2025-01-02 03:04:05', '2025-02-03 04:05:06'),
      ('user/lead.gin', 'legacy-lead-hash', 4, 'legacy lead', '',
       '2025-03-04 05:06:07', '2025-04-05 06:07:08'),
      ('presets/init.dmp', 'builtin-hash', 5, 'builtin', '',
       '2025-01-01 00:00:00', '2025-01-01 00:00:00')
  )");
  database.exec("INSERT INTO patch_tags (path, tag) VALUES "
                "('user/bass.gin', 'fm'), ('user/lead.gin', 'bright')");
}

void test_legacy_metadata_migration(const std::filesystem::path &root) {
  const auto test_root = root / "metadata-migration";
  const auto config = test_root / "config";
  const auto legacy_data = test_root / "legacy-data";
  const auto patches = legacy_data / "patches";
  std::filesystem::create_directories(config);
  std::filesystem::create_directories(patches / "user");
  std::ofstream(patches / "user" / "bass.gin") << "test";
  std::ofstream(patches / "user" / "lead.gin") << "test";
  create_legacy_metadata_database(config / "patch_metadata.db");

  // A sidecar written by a newer release is authoritative. The legacy import
  // may fill missing paths, but must not resurrect an older star/category.
  patches::FolderMetadataStore existing(patches / ".megatoy" / "patches.json");
  CHECK(existing.load());
  patches::PatchMetadata current;
  current.path = "user/bass.gin";
  current.star_rating = 1;
  current.category = "current bass";
  CHECK(existing.put(current));

  {
    std::ofstream output(config / "preferences.json");
    output << nlohmann::json{{"data_directory", legacy_data.string()}}.dump(2);
  }

  NativeFileSystem fs;
  megatoy::system::PathService paths(fs, config);
  {
    PreferenceManager preferences(paths);
    CHECK(preferences.workspace().contains(patches));
  }

  patches::FolderMetadataStore migrated(patches / ".megatoy" / "patches.json");
  CHECK(migrated.load());
  const auto bass = migrated.get("user/bass.gin");
  CHECK(bass.has_value());
  CHECK(bass->star_rating == 1);
  CHECK(bass->category == "current bass");
  const auto lead = migrated.get("user/lead.gin");
  CHECK(lead.has_value());
  CHECK(lead->star_rating == 4);
  CHECK(lead->category == "legacy lead");
  CHECK(lead->hash == "legacy-lead-hash");
  CHECK(lead->tags == std::vector<std::string>{"bright"});
  CHECK(lead->created_at == "2025-03-04 05:06:07");
  CHECK(!migrated.get("presets/init.dmp").has_value());
  CHECK(std::filesystem::exists(config / "patch_metadata.db"));

  nlohmann::json preferences_json;
  {
    std::ifstream input(config / "preferences.json");
    input >> preferences_json;
  }
  CHECK(preferences_json.at("legacy_metadata_migration").get<int>() == 1);

  // Clearing a star after migration must not be undone on later launches.
  auto cleared = *lead;
  cleared.star_rating = 0;
  cleared.category = "current lead";
  CHECK(migrated.put(cleared));
  {
    PreferenceManager preferences(paths);
  }
  patches::FolderMetadataStore restarted(patches / ".megatoy" / "patches.json");
  CHECK(restarted.load());
  CHECK(restarted.get("user/lead.gin")->star_rating == 0);
  CHECK(restarted.get("user/lead.gin")->category == "current lead");
}

void test_legacy_metadata_waits_for_custom_folder(
    const std::filesystem::path &root) {
  const auto test_root = root / "metadata-late-folder";
  const auto config = test_root / "config";
  const auto patches = test_root / "custom-patches";
  std::filesystem::create_directories(config);
  std::filesystem::create_directories(patches / "user");
  std::ofstream(patches / "user" / "bass.gin") << "test";
  create_legacy_metadata_database(config / "patch_metadata.db");
  {
    std::ofstream output(config / "preferences.json");
    output << nlohmann::json{{"legacy_workspace_migration", 1},
                             {"legacy_metadata_migration", 0},
                             {"workspace_folders", nlohmann::json::array()}}
                  .dump(2);
  }

  NativeFileSystem fs;
  megatoy::system::PathService paths(fs, config);
  {
    PreferenceManager preferences(paths);
    CHECK(preferences.workspace().empty());

    nlohmann::json pending;
    std::ifstream input(config / "preferences.json");
    input >> pending;
    CHECK(pending.at("legacy_metadata_migration").get<int>() == 0);

    CHECK(preferences.add_workspace_folder(patches));
  }

  patches::FolderMetadataStore migrated(patches / ".megatoy" / "patches.json");
  CHECK(migrated.load());
  const auto bass = migrated.get("user/bass.gin");
  CHECK(bass.has_value());
  CHECK(bass->star_rating == 5);
  nlohmann::json completed;
  {
    std::ifstream input(config / "preferences.json");
    input >> completed;
  }
  CHECK(completed.at("legacy_metadata_migration").get<int>() == 1);
}

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

void test_normal_workspace_folder_removal(const std::filesystem::path &root) {
  NativeFileSystem fs;
  const auto config = root / "normal-removal-config";
  const auto folder = root / "normal-removal-folder";
  std::filesystem::create_directories(folder);
  megatoy::system::PathService paths(fs, config);
  PreferenceManager preferences(paths);
  CHECK(preferences.add_workspace_folder(folder));
  CHECK(preferences.remove_workspace_folder(folder / "."));
  CHECK(preferences.workspace().empty());
}

// Saving must land in the workspace folder, and its metadata must end up in
// the folder's own sidecar rather than anywhere global.
void test_save_and_metadata_roundtrip(TestEnvironment &env) {
  env.session.current_patch().name = "workspace test";
  env.session.current_patch().instrument.algorithm = 5;

  auto &repository = env.session.repository();
  const auto saved =
      repository.save_patch(env.session.current_patch(), "workspace test",
                            /*overwrite=*/true, ".ginpkg");
  CHECK(saved.status == patches::SavePatchResult::Status::Success);
  CHECK(saved.path.parent_path() == env.patches_folder);
  CHECK(std::filesystem::exists(saved.path));

  repository.refresh();
  const auto relative =
      repository.to_relative_path(saved.path).generic_string();

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

void test_default_save_format_is_gin(TestEnvironment &env) {
  const auto saved = env.session.repository().save_patch(
      env.session.current_patch(), "default format", /*overwrite=*/true, {});
  CHECK(saved.status == patches::SavePatchResult::Status::Success);
  CHECK(saved.path.extension() == ".gin");
}

void test_ginpkg_versions_appear_as_container_items(TestEnvironment &env) {
  auto patch = env.session.current_patch();
  patch.name = "package patch";
  const auto package_path = env.patches_folder / "versions.ginpkg";
  CHECK(patches::write_patch(patch, package_path));
  patch.instrument.algorithm = 6;
  CHECK(patches::write_patch(patch, package_path));

  env.session.repository().refresh();
  const patches::PatchEntry *container = nullptr;
  for (const auto &root : env.session.repository().tree()) {
    auto found = std::find_if(root.children.begin(), root.children.end(),
                              [&](const patches::PatchEntry &entry) {
                                return entry.full_path == package_path;
                              });
    if (found != root.children.end()) {
      container = &*found;
      break;
    }
  }
  CHECK(container != nullptr);
  CHECK(container->is_directory);
  CHECK(container->format == "ginpkg");
  CHECK(container->children.size() == 2);
  CHECK(container->children.front().container_item_id == "__current__");
  CHECK(container->children.front().source_relative_path ==
        container->relative_path);

  ym2612::Patch latest;
  ym2612::Patch previous;
  CHECK(env.session.repository().load_patch(container->children[0], latest));
  CHECK(env.session.repository().load_patch(container->children[1], previous));
  CHECK(latest.instrument.algorithm == 6);
  CHECK(previous.instrument.algorithm != latest.instrument.algorithm);

  // A package version is selected by its unique tree entry, while Save still
  // targets the shared parent package. Undo/redo must preserve both identities.
  const auto &version = container->children[1];
  env.session.set_current_patch_path(version.source_relative_path);
  CHECK(env.session.current_patch_selection_path() ==
        version.source_relative_path + "/latest");
  env.session.set_current_patch_selection_path(version.relative_path);
  CHECK(env.session.current_patch_path() == version.source_relative_path);
  CHECK(env.session.current_patch_selection_path() == version.relative_path);
  const auto snapshot = env.session.capture_snapshot();
  env.session.set_current_patch_path({});
  env.session.restore_snapshot(snapshot);
  CHECK(env.session.current_patch_path() == version.source_relative_path);
  CHECK(env.session.current_patch_selection_path() == version.relative_path);
}

// Save must not overwrite a file it cannot safely rewrite.
void test_save_in_place_rules(TestEnvironment &env) {
  auto &repository = env.session.repository();
  const auto saved = repository.save_patch(
      env.session.current_patch(), "in place", /*overwrite=*/true, ".gin");
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

} // namespace

int main() {
  const UIPreferences default_ui_preferences;
  CHECK(!default_ui_preferences.show_patch_editor);
  CHECK(!default_ui_preferences.show_patch_selector);

  const auto migration_root = std::filesystem::temp_directory_path() /
                              "megatoy_workspace_migration_tests";
  std::filesystem::remove_all(migration_root);
  std::filesystem::create_directories(migration_root / "home");
  ScopedTestHome test_home(migration_root / "home");

  test_fresh_and_legacy_default_workspace_migration(migration_root);
  test_legacy_data_directory_migration();
  test_legacy_metadata_migration(migration_root);
  test_legacy_metadata_waits_for_custom_folder(migration_root);

  // Keep the general subsystem fixture independent of the launch marker.
  std::filesystem::remove_all(migration_root / "home" / "Documents" /
                              "megatoy");
  test_normal_workspace_folder_removal(migration_root);
  TestEnvironment env;
  test_patch_snapshot_roundtrip(env);
  test_note_allocation(env);
  test_workspace_folder_is_visible(env);
  test_default_save_format_is_gin(env);
  test_ginpkg_versions_appear_as_container_items(env);
  test_save_and_metadata_roundtrip(env);
  test_save_in_place_rules(env);

  std::cout << "All subsystem tests passed\n";
  std::filesystem::remove_all(migration_root);
  return 0;
}
