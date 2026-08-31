#include "audio/audio_manager.hpp"
#include "audio/audio_transport.hpp"
#include "audio/load_meter.hpp"
#include "core/status.hpp"
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
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

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

void test_failed_preferences_load_does_not_complete_workspace_migration(
    const std::filesystem::path &root) {
  NativeFileSystem fs;
  const auto config = root / "corrupt-preferences-config";
  std::filesystem::create_directories(config);
  {
    std::ofstream output(config / "preferences.json");
    output << R"({"workspace_folders":[)";
  }

  // A later preference change may repair a file that failed to load. That
  // save must preserve the pending marker so the next launch still probes the
  // pre-workspace default folder.
  {
    megatoy::system::PathService paths(fs, config);
    PreferenceManager preferences(paths);
    CHECK(preferences.workspace().empty());
    CHECK(preferences.save_preferences());
  }

  nlohmann::json repaired;
  {
    std::ifstream input(config / "preferences.json");
    input >> repaired;
  }
  CHECK(repaired.at("legacy_workspace_migration").get<int>() == 0);

  const auto legacy_patches =
      root / "home" / "Documents" / "megatoy" / "patches";
  {
    megatoy::system::PathService paths(fs, config);
    PreferenceManager preferences(paths);
    CHECK(preferences.workspace().folders().size() == 1);
    CHECK(preferences.workspace().folders()[0].path ==
          std::filesystem::weakly_canonical(legacy_patches));
  }
  {
    std::ifstream input(config / "preferences.json");
    input >> repaired;
  }
  CHECK(repaired.at("legacy_workspace_migration").get<int>() == 1);
}

void test_velocity_sensitivity_preference_round_trip(
    const std::filesystem::path &root) {
  NativeFileSystem fs;
  const auto config = root / "velocity-preference-config";
  std::filesystem::remove_all(config);
  std::filesystem::create_directories(config);

  {
    megatoy::system::PathService paths(fs, config);
    PreferenceManager preferences(paths);
    auto ui = preferences.ui_preferences();
    CHECK(ui.velocity_sensitivity_depth == 100);
    ui.velocity_sensitivity_depth = 37;
    preferences.set_ui_preferences(ui);
  }

  nlohmann::json stored;
  {
    std::ifstream input(config / "preferences.json");
    input >> stored;
  }
  CHECK(stored.at("ui").at("velocity_sensitivity_depth").get<int>() == 37);

  {
    megatoy::system::PathService paths(fs, config);
    PreferenceManager preferences(paths);
    CHECK(preferences.ui_preferences().velocity_sensitivity_depth == 37);
  }
}

void test_envelope_reference_note_preference_round_trip(
    const std::filesystem::path &root) {
  NativeFileSystem fs;
  const auto config = root / "envelope-reference-note-config";
  std::filesystem::remove_all(config);
  std::filesystem::create_directories(config);

  {
    megatoy::system::PathService paths(fs, config);
    PreferenceManager preferences(paths);
    auto ui = preferences.ui_preferences();
    CHECK(ui.envelope_reference_midi_note == 60); // middle C
    ui.envelope_reference_midi_note = 72;         // C5
    preferences.set_ui_preferences(ui);
  }

  nlohmann::json stored;
  {
    std::ifstream input(config / "preferences.json");
    input >> stored;
  }
  CHECK(stored.at("ui").at("envelope_reference_midi_note").get<int>() == 72);

  {
    megatoy::system::PathService paths(fs, config);
    PreferenceManager preferences(paths);
    CHECK(preferences.ui_preferences().envelope_reference_midi_note == 72);
  }
}

void test_chip_type_preference_round_trip(const std::filesystem::path &root) {
  NativeFileSystem fs;
  const auto config = root / "chip-type-preference-config";
  std::filesystem::remove_all(config);
  std::filesystem::create_directories(config);

  {
    megatoy::system::PathService paths(fs, config);
    PreferenceManager preferences(paths);
    auto ui = preferences.ui_preferences();
    CHECK(ui.ym2612_chip_type == 0);
    ui.ym2612_chip_type = 1;
    preferences.set_ui_preferences(ui);
  }

  nlohmann::json stored;
  {
    std::ifstream input(config / "preferences.json");
    input >> stored;
  }
  CHECK(stored.at("ui").at("ym2612_chip_type").get<int>() == 1);

  {
    megatoy::system::PathService paths(fs, config);
    PreferenceManager preferences(paths);
    CHECK(preferences.ui_preferences().ym2612_chip_type == 1);
  }

  stored["ui"]["ym2612_chip_type"] = -7;
  {
    std::ofstream output(config / "preferences.json");
    output << stored.dump(2);
  }
  {
    megatoy::system::PathService paths(fs, config);
    PreferenceManager preferences(paths);
    CHECK(preferences.ui_preferences().ym2612_chip_type == 0);
  }

  stored["ui"]["ym2612_chip_type"] = 8;
  {
    std::ofstream output(config / "preferences.json");
    output << stored.dump(2);
  }
  {
    megatoy::system::PathService paths(fs, config);
    PreferenceManager preferences(paths);
    CHECK(preferences.ui_preferences().ym2612_chip_type == 1);
  }
}

void test_core_preference_round_trip(const std::filesystem::path &root) {
  NativeFileSystem fs;
  const auto config = root / "core-preference-config";
  std::filesystem::remove_all(config);
  std::filesystem::create_directories(config);

  {
    megatoy::system::PathService paths(fs, config);
    PreferenceManager preferences(paths);
    auto ui = preferences.ui_preferences();
    CHECK(ui.ym2612_core == static_cast<int>(ym2612::CoreType::Ymfm));
    ui.ym2612_core = static_cast<int>(ym2612::CoreType::Nuked);
    preferences.set_ui_preferences(ui);
  }

  nlohmann::json stored;
  {
    std::ifstream input(config / "preferences.json");
    input >> stored;
  }
  CHECK(stored.at("ui").at("ym2612_core").get<int>() ==
        static_cast<int>(ym2612::CoreType::Nuked));

  {
    megatoy::system::PathService paths(fs, config);
    PreferenceManager preferences(paths);
    CHECK(preferences.ui_preferences().ym2612_core ==
          static_cast<int>(ym2612::CoreType::Nuked));
  }

  // Out of range falls back to the default.
  stored["ui"]["ym2612_core"] = 9;
  {
    std::ofstream output(config / "preferences.json");
    output << stored.dump(2);
  }
  {
    megatoy::system::PathService paths(fs, config);
    PreferenceManager preferences(paths);
    CHECK(preferences.ui_preferences().ym2612_core ==
          static_cast<int>(ym2612::CoreType::Ymfm));
  }
}

void test_load_reading_preference_round_trip(
    const std::filesystem::path &root) {
  NativeFileSystem fs;
  const auto config = root / "load-reading-preference-config";
  std::filesystem::remove_all(config);
  std::filesystem::create_directories(config);

  {
    megatoy::system::PathService paths(fs, config);
    PreferenceManager preferences(paths);
    auto ui = preferences.ui_preferences();
    CHECK(ui.audio_load_reading == static_cast<int>(audio::LoadReading::Peak));
    ui.audio_load_reading =
        static_cast<int>(audio::LoadReading::PeakAndAverage);
    preferences.set_ui_preferences(ui);
  }

  nlohmann::json stored;
  {
    std::ifstream input(config / "preferences.json");
    input >> stored;
  }
  CHECK(stored.at("ui").at("audio_load_reading").get<int>() ==
        static_cast<int>(audio::LoadReading::PeakAndAverage));

  {
    megatoy::system::PathService paths(fs, config);
    PreferenceManager preferences(paths);
    CHECK(preferences.ui_preferences().audio_load_reading ==
          static_cast<int>(audio::LoadReading::PeakAndAverage));
  }

  // Out of range falls back to the default, whether it was never a setting
  // or is one a stored file may still carry.
  for (const int out_of_range : {2, 9, -1}) {
    stored["ui"]["audio_load_reading"] = out_of_range;
    {
      std::ofstream output(config / "preferences.json");
      output << stored.dump(2);
    }
    megatoy::system::PathService paths(fs, config);
    PreferenceManager preferences(paths);
    CHECK(preferences.ui_preferences().audio_load_reading ==
          static_cast<int>(audio::LoadReading::Peak));
  }
}

void test_last_patch_preference_round_trip(const std::filesystem::path &root) {
  NativeFileSystem fs;
  const auto config = root / "last-patch-preference-config";
  std::filesystem::remove_all(config);
  std::filesystem::create_directories(config);

  {
    megatoy::system::PathService paths(fs, config);
    PreferenceManager preferences(paths);
    CHECK(preferences.last_patch_path().empty());
    preferences.set_last_patch_path("patches/bass.gin");
  }

  nlohmann::json stored;
  {
    std::ifstream input(config / "preferences.json");
    input >> stored;
  }
  CHECK(stored.at("last_patch_path").get<std::string>() == "patches/bass.gin");

  {
    megatoy::system::PathService paths(fs, config);
    PreferenceManager preferences(paths);
    CHECK(preferences.last_patch_path() == "patches/bass.gin");
  }
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

const patches::PatchEntry *
find_patch_entry(const std::vector<patches::PatchEntry> &entries,
                 const std::filesystem::path &path) {
  for (const auto &entry : entries) {
    if (entry.full_path == path) {
      return &entry;
    }
    if (const auto *found = find_patch_entry(entry.children, path)) {
      return found;
    }
  }
  return nullptr;
}

class TestAudioTransport final : public AudioTransport {
public:
  bool start(std::uint32_t, RenderCallback callback) override {
    if (failed_starts_ < starts_to_fail_) {
      ++failed_starts_;
      return false;
    }
    callback_ = std::move(callback);
    active_ = true;
    ++start_count_;
    return true;
  }

  void stop() override {
    active_ = false;
    callback_ = {};
  }

  bool is_active() const override { return active_; }

  void set_buffer_frames(int frames) override { buffer_frames_ = frames; }
  int buffer_frames() const { return buffer_frames_; }
  int start_count() const { return start_count_; }

  /// Refuse the next `count` calls to start(), as a device that will not open
  /// at the requested size does.
  void refuse_next_starts(int count) {
    starts_to_fail_ = count;
    failed_starts_ = 0;
  }

  float render_ac_peak(uint32_t frames) {
    CHECK(callback_);
    std::vector<int16_t> pcm(static_cast<size_t>(frames) * 2, 0);
    const uint32_t bytes = frames * 2 * static_cast<uint32_t>(sizeof(int16_t));
    CHECK(callback_(bytes, pcm.data()) == bytes);

    double sum = 0.0;
    for (int16_t sample : pcm) {
      sum += sample;
    }
    const double mean = sum / static_cast<double>(pcm.size());

    double peak = 0.0;
    for (int16_t sample : pcm) {
      peak = std::max(peak, std::abs(static_cast<double>(sample) - mean));
    }
    return static_cast<float>(peak / 32768.0);
  }

private:
  RenderCallback callback_;
  bool active_ = false;
  int buffer_frames_ = 0;
  int start_count_ = 0;
  int starts_to_fail_ = 0;
  int failed_starts_ = 0;
};

ym2612::Patch make_session_audio_patch(uint8_t total_level) {
  ym2612::Patch patch;
  patch.name = "session audio";
  patch.instrument.algorithm = 7;
  for (auto &settings : patch.instrument.operators) {
    settings.attack_rate = 31;
    settings.release_rate = 15;
    settings.total_level = total_level;
    settings.multiple = 1;
  }
  return patch;
}

void test_session_applies_direct_patch_mutation(TestEnvironment &env) {
  auto transport = std::make_unique<TestAudioTransport>();
  auto *test_transport = transport.get();
  AudioManager audio(std::move(transport));
  patches::PatchSession session(env.directories, env.preferences, audio);
  CHECK(audio.initialize(44100));

  session.current_patch() = make_session_audio_patch(20);
  CHECK(session.apply_patch_to_audio_if_changed());
  CHECK(!session.apply_patch_to_audio_if_changed());

  PreferenceManager::UIPreferences prefs{};
  prefs.use_velocity = true;
  prefs.steal_oldest_note_when_full = true;
  CHECK(session.note_on(ym2612::Note::from_midi_note(60), 127, prefs));
  const float before = test_transport->render_ac_peak(4410);

  session.current_patch().instrument.operators[0].total_level = 110;
  CHECK(session.apply_patch_to_audio_if_changed());
  const float after = test_transport->render_ac_peak(4410);
  CHECK(after < before * 0.9f);
  CHECK(!session.apply_patch_to_audio_if_changed());

  session.current_patch().name = "name only";
  CHECK(!session.apply_patch_to_audio_if_changed());
}

void test_performance_commands_do_not_dirty_session(TestEnvironment &env) {
  auto transport = std::make_unique<TestAudioTransport>();
  auto *test_transport = transport.get();
  AudioManager audio(std::move(transport));
  patches::PatchSession session(env.directories, env.preferences, audio);
  CHECK(audio.initialize(44100));

  session.current_patch() = make_session_audio_patch(20);
  session.mark_as_clean();
  session.apply_patch_to_audio();
  test_transport->render_ac_peak(64);
  CHECK(!session.is_modified());
  CHECK(!session.apply_patch_to_audio_if_changed());
  const auto patch_before = session.current_patch();

  CHECK(audio.submit(audio::AudioCommand::pitch_bend(12288)));
  CHECK(audio.submit(audio::AudioCommand::mod_wheel(127)));
  CHECK(audio.submit(audio::AudioCommand::mod_wheel(0)));
  test_transport->render_ac_peak(64);

  CHECK(session.current_patch() == patch_before);
  CHECK(!session.is_modified());
  CHECK(!session.apply_patch_to_audio_if_changed());
}

// A buffer size change reopens the device while the app runs. The patch has
// to survive it, nothing may be left keyed on, and the load history cannot
// carry entries recorded at the previous size.
void test_buffer_size_change_reopens_the_device(TestEnvironment &env) {
  auto transport = std::make_unique<TestAudioTransport>();
  auto *test_transport = transport.get();
  AudioManager audio(std::move(transport));
  patches::PatchSession session(env.directories, env.preferences, audio);
  CHECK(audio.initialize(44100, 512));
  CHECK(audio.buffer_frames() == 512);
  CHECK(test_transport->buffer_frames() == 512);
  CHECK(test_transport->start_count() == 1);

  session.current_patch() = make_session_audio_patch(20);
  session.apply_patch_to_audio();

  PreferenceManager::UIPreferences prefs{};
  prefs.use_velocity = true;
  prefs.steal_oldest_note_when_full = true;
  const auto c4 = ym2612::Note::from_midi_note(60);
  CHECK(session.note_on(c4, 127, prefs));
  const float sounding = test_transport->render_ac_peak(4410);
  CHECK(sounding > 0.0f);
  CHECK(session.note_is_active(c4));
  CHECK(audio.load_meter().history().count > 0);

  // Nothing reopens while the frames the preference resolves to stay put.
  CHECK(audio.set_buffer_frames(512));
  CHECK(test_transport->start_count() == 1);

  CHECK(audio.set_buffer_frames(1024));
  CHECK(test_transport->start_count() == 2);
  CHECK(test_transport->buffer_frames() == 1024);
  CHECK(audio.buffer_frames() == 1024);
  CHECK(audio.is_running());

  // The note the reopen interrupted is released, not left sounding.
  CHECK(!session.note_is_active(c4));
  for (bool active : session.active_channels()) {
    CHECK(!active);
  }
  CHECK(audio.load_meter().history().count == 0);

  // The patch came across, so the same note plays at the same level.
  CHECK(session.note_on(c4, 127, prefs));
  const float after = test_transport->render_ac_peak(4410);
  CHECK(after > sounding * 0.5f);
  session.release_all_notes();

  // A device that will not open at the new size keeps the one that works.
  test_transport->refuse_next_starts(1);
  CHECK(!audio.set_buffer_frames(2048));
  CHECK(audio.is_running());
  CHECK(audio.buffer_frames() == 1024);
  CHECK(test_transport->buffer_frames() == 1024);
  CHECK(session.note_on(c4, 127, prefs));
  CHECK(test_transport->render_ac_peak(4410) > sounding * 0.5f);
  session.release_all_notes();
}

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

void test_session_path_survives_workspace_relabel(
    const std::filesystem::path &root) {
  NativeFileSystem fs;
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
      second, patch, patch.name, /*overwrite=*/true, ".gin");
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

void test_current_patch_rename_preserves_clean_identity(TestEnvironment &env) {
  auto patch = env.session.current_patch();
  patch.name = "embedded old name";
  const auto source_path = env.patches_folder / "session-old.gin";
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
  CHECK(std::filesystem::exists(env.patches_folder / "session-new.gin"));
  CHECK(std::filesystem::path(env.session.current_patch_path()).stem() ==
        "session-new");
  CHECK(env.session.current_patch().name == "session-new");
  CHECK(env.session.capture_snapshot().original_patch.name == "session-new");
  CHECK(!env.session.is_modified());
}

// Renaming a folder that holds the current patch has to carry the patch with
// it, and renaming an unrelated one has to leave it alone.
void test_folder_rename_follows_the_current_patch(TestEnvironment &env) {
  const auto folder = env.patches_folder / "pads";
  std::filesystem::create_directories(folder);
  std::filesystem::create_directories(env.patches_folder / "elsewhere");

  auto patch = env.session.current_patch();
  const auto source_path = folder / "warm.gin";
  CHECK(patches::write_patch(patch, source_path));
  // An empty directory does not show up in the tree, so the decoy needs
  // something in it too.
  CHECK(
      patches::write_patch(patch, env.patches_folder / "elsewhere" / "x.gin"));
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
                                           env.patches_folder / "elsewhere");
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
  CHECK(std::filesystem::exists(env.patches_folder / "warm pads" / "warm.gin"));
  CHECK(env.session.current_patch_path() != path_before);
  CHECK(std::filesystem::path(env.session.current_patch_path())
            .parent_path()
            .filename() == "warm pads");
  CHECK(env.session.current_patch().name == "warm");
}

// A workspace folder is registered by path, so renaming one has to move the
// registration with it or the folder comes back missing.
void test_workspace_root_rename_follows_the_preference(TestEnvironment &env) {
  const auto *root =
      find_patch_entry(env.session.repository().tree(), env.patches_folder);
  CHECK(root != nullptr);
  CHECK(root->is_directory);
  CHECK(env.preferences.workspace().contains(env.patches_folder));

  const auto entry = *root;
  CHECK(env.session.rename_patch(entry, "sound library"));

  const auto renamed = env.root / "sound library";
  CHECK(!std::filesystem::exists(env.patches_folder));
  CHECK(std::filesystem::exists(renamed));

  CHECK(!env.preferences.workspace().contains(env.patches_folder));
  CHECK(env.preferences.workspace().contains(renamed));
  CHECK(env.preferences.workspace().folders().size() == 1);

  // And the repository is looking at the new location.
  CHECK(find_patch_entry(env.session.repository().tree(), renamed) != nullptr);

  env.patches_folder = renamed;
}

void test_current_patch_path_recording(TestEnvironment &env) {
  env.preferences.set_last_patch_path("patches/remembered.gin");
  const auto url_patch = env.session.current_patch();
  env.session.set_current_patch(url_patch, {},
                                patches::PatchSession::RememberPatchPath::No);
  CHECK(env.preferences.last_patch_path() == "patches/remembered.gin");

  const auto source_path = env.patches_folder / "recorded.gin";
  env.session.set_current_patch_path(source_path);
  CHECK(
      env.preferences.last_patch_path() ==
      env.session.repository().to_relative_path(source_path).generic_string());

  env.session.set_current_patch_path({});
  CHECK(env.preferences.last_patch_path().empty());
}

void test_restore_last_patch(TestEnvironment &env) {
  auto patch = env.session.current_patch();
  patch.name = "embedded name";
  patch.instrument.algorithm = 6;
  const auto source_path = env.patches_folder / "restored.gin";
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

  const auto package_path = env.patches_folder / "restored-package.ginpkg";
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

void test_missing_last_patch_falls_back_silently(TestEnvironment &env) {
  env.session.initialize_patch_defaults();
  const auto fallback = env.session.capture_snapshot();
  const std::string missing = "patches/missing.gin";
  env.preferences.set_last_patch_path(missing);
  megatoy::status::clear();

  CHECK(!env.session.restore_patch(env.preferences.last_patch_path()));
  CHECK(env.session.capture_snapshot() == fallback);
  CHECK(env.preferences.last_patch_path() == missing);
  CHECK(megatoy::status::entries().empty());
}

void test_patch_snapshot_roundtrip(TestEnvironment &env) {
  auto before = env.session.capture_snapshot();
  env.session.current_patch().name = "modified";
  env.session.restore_snapshot(before);
  CHECK(env.session.current_patch().name == before.patch.name);
}

void test_create_patch_in_folder(TestEnvironment &env) {
  auto &session = env.session;
  const auto defaults = session.default_patch();

  CHECK(session.can_create_patch_in(env.patches_folder));
  // Only a folder the workspace owns and may be written to.
  CHECK(!session.can_create_patch_in(env.root));
  CHECK(session.create_patch_in(env.root, "nowhere", ".gin").is_error());

  const auto created =
      session.create_patch_in(env.patches_folder, "brand new", ".gin");
  CHECK(created.is_success());
  CHECK(created.path == env.patches_folder / "brand new.gin");
  CHECK(std::filesystem::exists(created.path));

  // What was written is the patch a fresh launch opens, named for its file.
  CHECK(session.current_patch().name == "brand new");
  CHECK(session.current_patch().global == defaults.global);
  CHECK(session.current_patch().channel == defaults.channel);
  CHECK(session.current_patch().instrument == defaults.instrument);

  // And it is open and selected exactly as a click in the browser leaves it.
  const auto relative =
      session.repository().to_relative_path(created.path).generic_string();
  CHECK(session.current_patch_path() == relative);
  CHECK(session.current_patch_selection_path() == relative);
  CHECK(!session.is_modified());
  CHECK(find_patch_entry(session.repository().tree(), created.path) != nullptr);

  // The name is taken in the format that claimed it, and free in any other.
  CHECK(session.create_patch_in(env.patches_folder, "brand new", ".gin")
            .is_error());
  CHECK(session.create_patch_in(env.patches_folder, "brand new", ".dmp")
            .is_success());

  // Names the filesystem would not take are refused before anything is
  // written, and nothing is left behind.
  CHECK(session.create_patch_in(env.patches_folder, "", ".gin").is_error());
  CHECK(session.create_patch_in(env.patches_folder, "bad/name", ".gin")
            .is_error());
  CHECK(!std::filesystem::exists(env.patches_folder / "bad"));

  // A folder nested inside a workspace folder is writable too.
  const auto nested = env.patches_folder / "nested";
  std::filesystem::create_directories(nested);
  session.repository().refresh();
  CHECK(session.can_create_patch_in(nested));
  const auto in_nested = session.create_patch_in(nested, "deep", ".gin");
  CHECK(in_nested.is_success());
  CHECK(in_nested.path == nested / "deep.gin");
  CHECK(session.current_patch_selection_path() ==
        session.repository().to_relative_path(in_nested.path).generic_string());

  std::filesystem::remove(env.patches_folder / "brand new.gin");
  std::filesystem::remove(env.patches_folder / "brand new.dmp");
  std::filesystem::remove_all(nested);
  session.repository().refresh();
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
  CHECK(default_ui_preferences.show_patch_editor);
  CHECK(default_ui_preferences.show_patch_selector);
  CHECK(default_ui_preferences.show_preferences);

  const auto migration_root = std::filesystem::temp_directory_path() /
                              "megatoy_workspace_migration_tests";
  std::filesystem::remove_all(migration_root);
  std::filesystem::create_directories(migration_root / "home");
  ScopedTestHome test_home(migration_root / "home");

  test_fresh_and_legacy_default_workspace_migration(migration_root);
  test_failed_preferences_load_does_not_complete_workspace_migration(
      migration_root);
  test_velocity_sensitivity_preference_round_trip(migration_root);
  test_chip_type_preference_round_trip(migration_root);
  test_core_preference_round_trip(migration_root);
  test_load_reading_preference_round_trip(migration_root);
  test_envelope_reference_note_preference_round_trip(migration_root);
  test_last_patch_preference_round_trip(migration_root);
  test_legacy_data_directory_migration();
  test_legacy_metadata_migration(migration_root);
  test_legacy_metadata_waits_for_custom_folder(migration_root);

  // Keep the general subsystem fixture independent of the launch marker.
  std::filesystem::remove_all(migration_root / "home" / "Documents" /
                              "megatoy");
  test_normal_workspace_folder_removal(migration_root);
  test_session_path_survives_workspace_relabel(migration_root);
  TestEnvironment env;
  test_session_applies_direct_patch_mutation(env);
  test_performance_commands_do_not_dirty_session(env);
  test_patch_snapshot_roundtrip(env);
  test_note_allocation(env);
  test_buffer_size_change_reopens_the_device(env);
  test_workspace_folder_is_visible(env);
  test_default_save_format_is_gin(env);
  test_ginpkg_versions_appear_as_container_items(env);
  test_save_and_metadata_roundtrip(env);
  test_save_in_place_rules(env);
  test_current_patch_rename_preserves_clean_identity(env);
  test_current_patch_path_recording(env);
  test_folder_rename_follows_the_current_patch(env);
  test_workspace_root_rename_follows_the_preference(env);
  test_restore_last_patch(env);
  test_missing_last_patch_falls_back_silently(env);
  test_create_patch_in_folder(env);

  std::cout << "All subsystem tests passed\n";
  std::filesystem::remove_all(migration_root);
  return 0;
}
