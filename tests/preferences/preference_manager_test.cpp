#include "../test_check.hpp"
#include "platform/std_file_system.hpp"
#include "preferences/preference_manager.hpp"
#include "system/path_service.hpp"

#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct TestEnvironment {
  fs::path root;
  platform::StdFileSystem file_system;
  megatoy::system::PathService directories;

  explicit TestEnvironment(const fs::path &path)
      : root(path), directories(file_system, root / "config") {
    fs::create_directories(root / "config");
    std::ofstream stream(directories.paths().preferences_file);
    stream << "{}";
    CHECK(stream.good());
  }
};

void test_settings_are_persisted_without_waiting_for_destruction(
    const fs::path &root) {
  TestEnvironment env(root);
  PreferenceManager preferences(env.directories);
  CHECK(preferences.is_initialized());
  CHECK(preferences.get_imgui_ini_file() ==
        env.directories.paths().imgui_ini_file);
  preferences.set_last_patch_path(
      (fs::path("Bass") / "warm.gin").generic_string());
  preferences.set_last_seen_version("v1.2.3");
  preferences.set_show_builtin_presets(false);
  preferences.set_theme(ui::styles::ThemeId::ImGuiLight);
  const std::vector<OpenPatchRecord> records{{"warm.gin", true},
                                             {"cold.gin", false}};
  preferences.set_open_patches(records, 1);
  auto ui = preferences.ui_preferences();
  ui.ui_scale = 1.5f;
  ui.show_waveform = false;
  ui.use_mod_wheel = true;
  preferences.set_ui_preferences(ui);
  preferences.set_ui_preferences(ui);
  preferences.set_last_seen_version("v1.2.3");
  PreferenceManager loaded(env.directories);
  CHECK(loaded.last_patch_path() == preferences.last_patch_path());
  CHECK(loaded.last_seen_version() == "v1.2.3");
  CHECK(!loaded.show_builtin_presets());
  CHECK(loaded.theme() == ui::styles::ThemeId::ImGuiLight);
  CHECK(loaded.open_patches() == records);
  CHECK(loaded.load_preferences());
  CHECK(loaded.open_patches() == records);
  CHECK(loaded.load_preferences());
  CHECK(loaded.open_patches() == records);
  CHECK(loaded.active_open_patch() == 1);
  CHECK(loaded.ui_preferences() == ui);
}

void test_workspace_changes_survive_reload_and_reject_invalid_moves(
    const fs::path &root) {
  TestEnvironment env(root);
  const auto a = fs::weakly_canonical(root / "A");
  const auto b = fs::weakly_canonical(root / "B");
  const auto renamed = fs::weakly_canonical(root / "Renamed");
  fs::create_directories(a);
  fs::create_directories(b);
  PreferenceManager preferences(env.directories);
  CHECK(preferences.add_workspace_folder(a));
  CHECK(preferences.add_workspace_folder(b));
  CHECK(!preferences.add_workspace_folder(a));
  CHECK(!preferences.reorder_workspace_folder(0, 9));
  CHECK(!preferences.remove_workspace_folder(root / "absent"));
  CHECK(!preferences.rename_workspace_folder(root / "absent", renamed));
  CHECK(preferences.reorder_workspace_folder(1, 0));
  fs::rename(a, renamed);
  CHECK(preferences.rename_workspace_folder(a, renamed));
  {
    PreferenceManager loaded(env.directories);
    CHECK((loaded.workspace().paths() == std::vector<fs::path>{b, renamed}));
  }
  CHECK(preferences.remove_workspace_folder(b));
  PreferenceManager loaded(env.directories);
  CHECK((loaded.workspace().paths() == std::vector<fs::path>{renamed}));
}

void test_last_save_directory_falls_back_when_the_remembered_folder_is_gone(
    const fs::path &root) {
  TestEnvironment env(root);
  const auto workspace = fs::weakly_canonical(root / "patches");
  const auto exported = root / "exports";
  fs::create_directories(workspace);
  fs::create_directories(exported);
  PreferenceManager preferences(env.directories);
  CHECK(preferences.last_save_directory() ==
        megatoy::system::PathService::default_documents_directory());
  CHECK(preferences.add_workspace_folder(workspace));
  CHECK(preferences.last_save_directory() == workspace);
  preferences.set_last_save_directory(exported);
  preferences.set_last_save_directory({});
  CHECK(preferences.last_save_directory() == exported);
  {
    PreferenceManager loaded(env.directories);
    CHECK(loaded.last_save_directory() == exported);
  }
  fs::remove_all(exported);
  CHECK(preferences.last_save_directory() == workspace);
  fs::remove_all(workspace);
  CHECK(preferences.refresh_workspace_availability());
  CHECK(!preferences.refresh_workspace_availability());
  CHECK(preferences.last_save_directory() ==
        megatoy::system::PathService::default_documents_directory());
}

void test_malformed_reload_and_failed_save_preserve_current_settings(
    const fs::path &root) {
  TestEnvironment env(root);
  PreferenceManager preferences(env.directories);
  preferences.set_last_seen_version("v1.0.0");
  auto ui = preferences.ui_preferences();
  ui.ui_scale = 2.0f;
  preferences.set_ui_preferences(ui);
  const auto file = env.directories.paths().preferences_file;
  {
    std::ofstream stream(file);
    stream << "{broken JSON";
    CHECK(stream.good());
  }
  CHECK(!preferences.load_preferences());
  CHECK(preferences.last_seen_version() == "v1.0.0");
  CHECK(preferences.ui_preferences() == ui);
  fs::remove(file);
  fs::create_directory(file);
  CHECK(!preferences.save_preferences());
  CHECK(preferences.ui_preferences() == ui);
  fs::remove(file);
  CHECK(preferences.save_preferences());
  PreferenceManager loaded(env.directories);
  CHECK(loaded.last_seen_version() == "v1.0.0");
  CHECK(loaded.ui_preferences() == ui);
}

void test_windows_are_shown_by_default() {
  const UIPreferences preferences;
  CHECK(preferences.show_patch_selector);
  CHECK(preferences.show_preferences);
}

void test_fresh_workspace(const std::filesystem::path &root) {
  platform::StdFileSystem fs;

  const auto fresh_config = root / "fresh-config";
  {
    megatoy::system::PathService paths(fs, fresh_config);
    PreferenceManager preferences(paths);
    CHECK(preferences.workspace().empty());
  }
}

void test_failed_preferences_load_can_be_repaired(
    const std::filesystem::path &root) {
  platform::StdFileSystem fs;
  const auto config = root / "corrupt-preferences-config";
  std::filesystem::create_directories(config);
  {
    std::ofstream output(config / "preferences.json");
    output << R"({"workspace_folders":[)";
  }

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
  CHECK(repaired.at("workspace_folders").empty());
}

void test_velocity_sensitivity_preference_round_trip(
    const std::filesystem::path &root) {
  platform::StdFileSystem fs;
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

void test_chip_type_preference_round_trip(const std::filesystem::path &root) {
  platform::StdFileSystem fs;
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
  platform::StdFileSystem fs;
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
  platform::StdFileSystem fs;
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

void test_envelope_reference_note_preference_round_trip(
    const std::filesystem::path &root) {
  platform::StdFileSystem fs;
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

void test_last_patch_preference_round_trip(const std::filesystem::path &root) {
  platform::StdFileSystem fs;
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

void test_normal_workspace_folder_removal(const std::filesystem::path &root) {
  platform::StdFileSystem fs;
  const auto config = root / "normal-removal-config";
  const auto folder = root / "normal-removal-folder";
  std::filesystem::create_directories(folder);
  megatoy::system::PathService paths(fs, config);
  PreferenceManager preferences(paths);
  CHECK(preferences.add_workspace_folder(folder));
  CHECK(preferences.remove_workspace_folder(folder / "."));
  CHECK(preferences.workspace().empty());
}

} // namespace

int main() {
  const auto root =
      fs::temp_directory_path() / "megatoy_preference_manager_test";
  fs::remove_all(root);
  fs::create_directories(root);
  test_settings_are_persisted_without_waiting_for_destruction(root /
                                                              "settings");
  test_workspace_changes_survive_reload_and_reject_invalid_moves(root /
                                                                 "workspace");
  test_last_save_directory_falls_back_when_the_remembered_folder_is_gone(
      root / "save");
  test_malformed_reload_and_failed_save_preserve_current_settings(root /
                                                                  "failures");
  test_windows_are_shown_by_default();
  test_fresh_workspace(root);
  test_failed_preferences_load_can_be_repaired(root);
  test_velocity_sensitivity_preference_round_trip(root);
  test_chip_type_preference_round_trip(root);
  test_core_preference_round_trip(root);
  test_load_reading_preference_round_trip(root);
  test_envelope_reference_note_preference_round_trip(root);
  test_last_patch_preference_round_trip(root);
  test_normal_workspace_folder_removal(root);
  fs::remove_all(root);
  std::cout << "All preference manager tests passed\n";
  return 0;
}
