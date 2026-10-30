#include "../test_check.hpp"
#include "platform/std_file_system.hpp"
#include "preferences/preference_manager.hpp"
#include "system/path_service.hpp"

#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <iostream>

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
  preferences.set_theme(ui::styles::ThemeId::MegatoyLight);
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
  CHECK(loaded.theme() == ui::styles::ThemeId::MegatoyLight);
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
  fs::remove_all(root);
  std::cout << "All preference manager tests passed\n";
  return 0;
}
